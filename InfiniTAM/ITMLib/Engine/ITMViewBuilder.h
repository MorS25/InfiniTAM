// Copyright 2014 Isis Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../Utils/ITMLibDefines.h"
#include "../Utils/ITMLibSettings.h"

#include "../Objects/ITMView.h"
#include "../Objects/ITMRGBDCalib.h"

using namespace ITMLib::Objects;

namespace ITMLib
{
	namespace Engine
	{
		/** \brief
		*/
		class ITMViewBuilder
		{
		private:
			const ITMLib::Objects::ITMRGBDCalib *calib;
			ITMLib::Objects::ITMLibSettings::DeviceType deviceType;
			ITMShortImage *shortImage;

			void allocateView(ITMView *view, Vector2i imgSize_rgb, Vector2i imgSize_d, bool useGPU, bool allocateShort);

		public:
			enum InputImageType
			{
				//! Raw disparity images as received from the
				//! Kinect
				InfiniTAM_DISPARITY_IMAGE,
				//! Short valued depth image in millimetres
				InfiniTAM_SHORT_DEPTH_IMAGE,
				//! Floating point valued depth images in meters
				InfiniTAM_FLOAT_DEPTH_IMAGE
			}inputImageType;

			virtual void ConvertDisparityToDepth(ITMFloatImage *depth_out, const ITMShortImage *disp_in, const ITMIntrinsics *depthIntrinsics,
				const ITMDisparityCalib *disparityCalib) = 0;
			virtual void ConvertDepthMMToFloat(ITMFloatImage *depth_out, const ITMShortImage *depth_in) = 0;

			void UpdateView(ITMView* view, ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage);
			void UpdateView(ITMView *view, ITMUChar4Image *rgbImage, ITMFloatImage *depthImage);

			ITMViewBuilder(const ITMRGBDCalib *calib, ITMLibSettings::DeviceType deviceType);
			virtual ~ITMViewBuilder();
		};
	}
}
