// Copyright 2014-2015 Isis Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../../Utils/ITMLibDefines.h"
#include "ITMPixelUtils.h"
#include "ITMRepresentationAccess.h"

template<class TVoxel>
_CPU_AND_GPU_CODE_ inline float computeUpdatedVoxelDepthInfo(
		DEVICEPTR(TVoxel) &voxel,
		const THREADPTR(Vector4f) &pt_model,
		const CONSTPTR(Matrix4f) &M_d,
		const CONSTPTR(Vector4f) & projParams_d,
		float mu, int maxW,
		const CONSTPTR(float) *depth,
		const CONSTPTR(Vector2i) &imgSize
) {
	Vector4f pt_camera; Vector2f pt_image;
	float depth_measure, eta, oldF, newF;
	int oldW, newW;

	// project point into image
	pt_camera = M_d * pt_model;
	if (pt_camera.z <= 0) return -1;

	pt_image.x = projParams_d.x * pt_camera.x / pt_camera.z + projParams_d.z;
	pt_image.y = projParams_d.y * pt_camera.y / pt_camera.z + projParams_d.w;
	if ((pt_image.x < 1) || (pt_image.x > imgSize.x - 2) ||
		(pt_image.y < 1) || (pt_image.y > imgSize.y - 2)) {
		// Not in the image
		return -1;
	}

	// get measured depth from image
	depth_measure = depth[(int)(pt_image.x + 0.5f) + (int)(pt_image.y + 0.5f) * imgSize.x];
	if (depth_measure <= 0.0) {
		// No depth information available at this position, so nothing to fuse.
		return -1;
	}

	// check whether voxel needs updating
	eta = depth_measure - pt_camera.z;
	if (eta < -mu) {
		// The measurement is too far away from existing surface, so we clamp it.
		return eta;
	}

	// compute updated SDF value and reliability
	oldF = TVoxel::SDF_valueToFloat(voxel.sdf); oldW = voxel.w_depth;

	newF = MIN(1.0f, eta / mu);
	newW = 1;

	newF = oldW * oldF + newW * newF;
	newW = oldW + newW;
	newF /= newW;
	newW = MIN(newW, maxW);

	// write back
	voxel.sdf = TVoxel::SDF_floatToValue(newF);
	voxel.w_depth = newW;

	return eta;
}


template<class TVoxel>
_CPU_AND_GPU_CODE_ inline void computeUpdatedVoxelColorInfo(DEVICEPTR(TVoxel) &voxel, const THREADPTR(Vector4f) & pt_model, const CONSTPTR(Matrix4f) & M_rgb,
	const CONSTPTR(Vector4f) & projParams_rgb, float mu, uchar maxW, float eta, const CONSTPTR(Vector4u) *rgb, const CONSTPTR(Vector2i) & imgSize)
{
	Vector4f pt_camera; Vector2f pt_image;
	Vector3f rgb_measure, oldC, newC; Vector3u buffV3u;

	// TODO(andrei): If you want to pass in custom weights, looks like this is the place to do it.
	float newW, oldW;

	buffV3u = voxel.clr;
	oldW = (float)voxel.w_color;

	oldC = TO_FLOAT3(buffV3u) / 255.0f;
	newC = oldC;

	pt_camera = M_rgb * pt_model;

	pt_image.x = projParams_rgb.x * pt_camera.x / pt_camera.z + projParams_rgb.z;
	pt_image.y = projParams_rgb.y * pt_camera.y / pt_camera.z + projParams_rgb.w;

	if ((pt_image.x < 1) || (pt_image.x > imgSize.x - 2) || (pt_image.y < 1) || (pt_image.y > imgSize.y - 2)) return;

	rgb_measure = TO_VECTOR3(interpolateBilinear(rgb, pt_image, imgSize)) / 255.0f;
	//rgb_measure = rgb[(int)(pt_image.x + 0.5f) + (int)(pt_image.y + 0.5f) * imgSize.x].toVector3().toFloat() / 255.0f;
	newW = 1;

	newC = oldC * oldW + rgb_measure * newW;
	newW = oldW + newW;
	newC /= newW;
	newW = MIN(newW, maxW);

	buffV3u = TO_UCHAR3(newC * 255.0f);

	voxel.clr = buffV3u;
	voxel.w_color = (uchar)newW;
}

template<bool hasColor, class TVoxel> struct ComputeUpdatedVoxelInfo;

template<class TVoxel>
struct ComputeUpdatedVoxelInfo<false, TVoxel> {
	_CPU_AND_GPU_CODE_ static void compute(DEVICEPTR(TVoxel) & voxel, const THREADPTR(Vector4f) & pt_model,
		const CONSTPTR(Matrix4f) & M_d, const CONSTPTR(Vector4f) & projParams_d,
		const CONSTPTR(Matrix4f) & M_rgb, const CONSTPTR(Vector4f) & projParams_rgb,
		float mu, int maxW,
		const CONSTPTR(float) *depth, const CONSTPTR(Vector2i) & imgSize_d,
		const CONSTPTR(Vector4u) *rgb, const CONSTPTR(Vector2i) & imgSize_rgb)
	{
		computeUpdatedVoxelDepthInfo(voxel, pt_model, M_d, projParams_d, mu, maxW, depth, imgSize_d);
	}
};

template<class TVoxel>
struct ComputeUpdatedVoxelInfo<true, TVoxel> {
	_CPU_AND_GPU_CODE_ static void compute(
		DEVICEPTR(TVoxel) & voxel,
		const THREADPTR(Vector4f) & pt_model,
		const THREADPTR(Matrix4f) & M_d,
		const THREADPTR(Vector4f) & projParams_d,
		const THREADPTR(Matrix4f) & M_rgb,
		const THREADPTR(Vector4f) & projParams_rgb,
		float mu,
		int maxW,
		const CONSTPTR(float) *depth,
		const CONSTPTR(Vector2i) & imgSize_d,
		const CONSTPTR(Vector4u) *rgb,
		const THREADPTR(Vector2i) & imgSize_rgb)
	{
		float eta = computeUpdatedVoxelDepthInfo(voxel, pt_model, M_d, projParams_d, mu, maxW, depth, imgSize_d);
		if ((eta > mu) || (fabs(eta / mu) > 0.25f)) {
			return;
		}

		computeUpdatedVoxelColorInfo(voxel, pt_model, M_rgb, projParams_rgb, mu, maxW, eta, rgb, imgSize_rgb);
	}
};

// TODO(andrei): Change back to hybrid code. _CPU_AND_GPU_CODE_
__device__
inline void buildHashAllocAndVisibleTypePP(
		DEVICEPTR(uchar) *entriesAllocType,
		DEVICEPTR(uchar) *entriesVisibleType,
		int x,
		int y,
		DEVICEPTR(Vector4s) *blockCoords,
		const CONSTPTR(float) *depth,
		Matrix4f invM_d,
		Vector4f projParams_d,
		float mu,
		Vector2i imgSize,
		float oneOverVoxelSize,
		const CONSTPTR(ITMHashEntry) *hashTable,
		float viewFrustum_min,
		float viewFrustum_max,
		int *locks
) {
	float depth_measure; unsigned int hashIdx; int noSteps;
	Vector3f pt_camera_f, point_e, point, direction; Vector3s blockPos;

	depth_measure = depth[x + y * imgSize.x];

	if (depth_measure <= 0 || (depth_measure - mu) < 0 || (depth_measure - mu) < viewFrustum_min || (depth_measure + mu) > viewFrustum_max) return;

	// This triangulates the point's position from x, y, and depth.
	pt_camera_f.z = depth_measure;
	pt_camera_f.x = pt_camera_f.z * ((float(x) - projParams_d.z) * projParams_d.x);
	pt_camera_f.y = pt_camera_f.z * ((float(y) - projParams_d.w) * projParams_d.y);

	float norm = sqrt(pt_camera_f.x * pt_camera_f.x +pt_camera_f.y * pt_camera_f.y + pt_camera_f.z * pt_camera_f.z);

	Vector4f tmp;
	tmp.x = pt_camera_f.x * (1.0f - mu / norm);
	tmp.y = pt_camera_f.y * (1.0f - mu / norm);
	tmp.z = pt_camera_f.z * (1.0f - mu / norm);
	tmp.w = 1.0f;
	point = TO_VECTOR3(invM_d * tmp) * oneOverVoxelSize;
	tmp.x = pt_camera_f.x * (1.0f + mu / norm);
	tmp.y = pt_camera_f.y * (1.0f + mu / norm);
	tmp.z = pt_camera_f.z * (1.0f + mu / norm);
	point_e = TO_VECTOR3(invM_d * tmp) * oneOverVoxelSize;

	direction = point_e - point;
	norm = sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
	noSteps = (int)ceil(2.0f*norm);

	direction /= (float)(noSteps - 1);

	// TODO(andrei): Inspect this code to see if you can go for finer-grained fusion by taking smaller steps.
	//add neighbouring blocks
	for (int i = 0; i < noSteps; i++)
	{
		blockPos = TO_SHORT_FLOOR3(point);

		//compute index in hash table
		hashIdx = hashIndex(blockPos);

		//check if hash table contains entry
		bool isFound = false;

		ITMHashEntry hashEntry = hashTable[hashIdx];

#if defined(__CUDACC__) && defined(__CUDA_ARCH__)
      int key = hashIdx;
		int contention = atomicExch(&locks[key], 1);
		if (contention != 0) {
			if(key % 100 == 33) {
//				printf("Contention for key %d when allocating! Deferring allocation for block (%d, %d, %d)\n",
//				key,
//				static_cast<int>(blockPos.x),
//				static_cast<int>(blockPos.y),
//				static_cast<int>(blockPos.z));
			}
			// Fight me bro!
			goto loop_end;
		}
#endif
		if (IS_EQUAL3(hashEntry.pos, blockPos) && hashEntry.ptr >= -1)
		{
			//entry (has been streamed out but is visible) or (in memory and visible)
			entriesVisibleType[hashIdx] = (hashEntry.ptr == -1) ? 2 : 1;

			isFound = true;
		}

		if (!isFound)
		{
			bool isExcess = false;
			if (hashEntry.ptr >= -1) // search excess list only if there is no room in ordered part
			{
				while (hashEntry.offset >= 1)
				{
					// Get the next element in the excess list by inspecting the appropriate hash
					// table element.
					hashIdx = SDF_BUCKET_NUM + hashEntry.offset - 1;
					hashEntry = hashTable[hashIdx];

					if (IS_EQUAL3(hashEntry.pos, blockPos) && hashEntry.ptr >= -1)
					{
						//entry has been streamed out but is visible or in memory and visible
						entriesVisibleType[hashIdx] = (hashEntry.ptr == -1) ? 2 : 1;

						isFound = true;
						break;
					}
				}

				isExcess = true;
			}

			if (!isFound) //still not found
			{
				// TODO(andrei): It seems there may be a data race here. Namely, it seems that
				// (albeit with low probability) it may be that two different blocks *think* they
				// ought to be allocated in the VBA, when, in fact, one of them should be put in the
				// VBA and one in the excess list, after it. This is discussed in the InfiniTAM
				// technical report.

				// TODO(andrei): Remove this crap.
#if defined(__CUDACC__) && defined(__CUDA_ARCH__)
				if (hashEntry.ptr == -3 && threadIdx.x == 0 && threadIdx.y == 0 && threadIdx.z == 0) {
					printf("Allocating block (%d, %d, %d) into recycled %s hash table slot\n",
						   static_cast<int>(blockPos.x),
						   static_cast<int>(blockPos.y),
						   static_cast<int>(blockPos.z),
						   isExcess ? "excess" : "ordered"
					);
				}
#endif

				// TODO(andrei): Could we detect allocation failures here?
				entriesAllocType[hashIdx] = isExcess ? 2 : 1; 		// needs allocation
				if (!isExcess) entriesVisibleType[hashIdx] = 1; 	//new entry is visible

				blockCoords[hashIdx] = Vector4s(blockPos.x, blockPos.y, blockPos.z, 1);
			}
		}

#if defined(__CUDACC__) && defined(__CUDA_ARCH__)
	// Release the lock on the bucket
	atomicExch(&locks[key], 0);
loop_end:
#endif
		point += direction;
	}
}

template<bool useSwapping>
_CPU_AND_GPU_CODE_ inline void checkPointVisibility(THREADPTR(bool) &isVisible, THREADPTR(bool) &isVisibleEnlarged,
	const THREADPTR(Vector4f) &pt_image, const CONSTPTR(Matrix4f) & M_d, const CONSTPTR(Vector4f) &projParams_d,
	const CONSTPTR(Vector2i) &imgSize)
{
	Vector4f pt_buff;

	pt_buff = M_d * pt_image;

	// The point is right next to the camera or behind it.
	if (pt_buff.z < 1e-10f) return;

	// Next, we check if it's inside the camera's viewport.
	pt_buff.x = projParams_d.x * pt_buff.x / pt_buff.z + projParams_d.z;
	pt_buff.y = projParams_d.y * pt_buff.y / pt_buff.z + projParams_d.w;

	if (pt_buff.x >= 0 && pt_buff.x < imgSize.x && pt_buff.y >= 0 && pt_buff.y < imgSize.y)
	{
		isVisible = true;
		isVisibleEnlarged = true;
	}
	else if (useSwapping)
	{
		Vector4i lims;
		lims.x = -imgSize.x / 8; lims.y = imgSize.x + imgSize.x / 8;
		lims.z = -imgSize.y / 8; lims.w = imgSize.y + imgSize.y / 8;

		if (pt_buff.x >= lims.x && pt_buff.x < lims.y && pt_buff.y >= lims.z && pt_buff.y < lims.w) isVisibleEnlarged = true;
	}
}

/// \brief Considers a block to be visible if any of its corners is visible.
template<bool useSwapping>
_CPU_AND_GPU_CODE_ inline void checkBlockVisibility(THREADPTR(bool) &isVisible, THREADPTR(bool) &isVisibleEnlarged,
	const THREADPTR(Vector3s) &hashPos, const CONSTPTR(Matrix4f) & M_d, const CONSTPTR(Vector4f) &projParams_d,
	const CONSTPTR(float) &voxelSize, const CONSTPTR(Vector2i) &imgSize)
{
	Vector4f pt_image;
	float factor = (float)SDF_BLOCK_SIZE * voxelSize;

	isVisible = false; isVisibleEnlarged = false;

	// 0 0 0
	pt_image.x = (float)hashPos.x * factor; pt_image.y = (float)hashPos.y * factor;
	pt_image.z = (float)hashPos.z * factor; pt_image.w = 1.0f;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 0 0 1
	pt_image.z += factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 0 1 1
	pt_image.y += factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 1 1 1
	pt_image.x += factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 1 1 0 
	pt_image.z -= factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 1 0 0 
	pt_image.y -= factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 0 1 0
	pt_image.x -= factor; pt_image.y += factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;

	// 1 0 1
	pt_image.x += factor; pt_image.y -= factor; pt_image.z += factor;
	checkPointVisibility<useSwapping>(isVisible, isVisibleEnlarged, pt_image, M_d, projParams_d, imgSize);
	if (isVisible) return;
}
