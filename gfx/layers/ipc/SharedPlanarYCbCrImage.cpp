/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedPlanarYCbCrImage.h"
#include <stddef.h>                     // for size_t
#include <stdio.h>                      // for printf
#include "gfx2DGlue.h"                  // for Moz2D transition helpers
#include "ISurfaceAllocator.h"          // for ISurfaceAllocator, etc
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/gfx/Types.h"          // for SurfaceFormat::SurfaceFormat::YUV
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/layers/ImageClient.h"  // for ImageClient
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc
#include "mozilla/layers/TextureClient.h"  // for BufferTextureClient, etc
#include "mozilla/layers/YCbCrImageDataSerializer.h"
#include "mozilla/layers/ImageBridgeChild.h"  // for ImageBridgeChild
#include "mozilla/mozalloc.h"           // for operator delete
#include "nsISupportsImpl.h"            // for Image::AddRef
#include "mozilla/ipc/Shmem.h"

class gfxASurface;

namespace mozilla {
namespace layers {

using namespace mozilla::ipc;

SharedPlanarYCbCrImage::SharedPlanarYCbCrImage(ImageClient* aCompositable)
: PlanarYCbCrImage(nullptr)
, mCompositable(aCompositable)
{
  mTextureClient = aCompositable->CreateBufferTextureClient(gfx::SurfaceFormat::YUV);
  MOZ_COUNT_CTOR(SharedPlanarYCbCrImage);
}

SharedPlanarYCbCrImage::~SharedPlanarYCbCrImage() {
  MOZ_COUNT_DTOR(SharedPlanarYCbCrImage);

  if (mCompositable->GetAsyncID() != 0 &&
      !InImageBridgeChildThread()) {
    ImageBridgeChild::DispatchReleaseTextureClient(mTextureClient.forget().drop());
    ImageBridgeChild::DispatchReleaseImageClient(mCompositable.forget().drop());
  }
}

size_t
SharedPlanarYCbCrImage::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
  // NB: Explicitly skipping mTextureClient, the memory is already reported
  //     at time of allocation in GfxMemoryImageReporter.
  // Not owned:
  // - mCompositable
  size_t size = PlanarYCbCrImage::SizeOfExcludingThis(aMallocSizeOf);
  return size;
}

TextureClient*
SharedPlanarYCbCrImage::GetTextureClient(CompositableClient* aClient)
{
  return mTextureClient.get();
}

uint8_t*
SharedPlanarYCbCrImage::GetBuffer()
{
  return mTextureClient->GetBuffer();
}

already_AddRefed<gfxASurface>
SharedPlanarYCbCrImage::DeprecatedGetAsSurface()
{
  if (!mTextureClient->IsAllocated()) {
    NS_WARNING("Can't get as surface");
    return nullptr;
  }
  return PlanarYCbCrImage::DeprecatedGetAsSurface();
}

TemporaryRef<gfx::SourceSurface>
SharedPlanarYCbCrImage::GetAsSourceSurface()
{
  if (!mTextureClient->IsAllocated()) {
    NS_WARNING("Can't get as surface");
    return nullptr;
  }
  return PlanarYCbCrImage::GetAsSourceSurface();
}

void
SharedPlanarYCbCrImage::SetData(const PlanarYCbCrData& aData)
{
  // If mShmem has not been allocated (through Allocate(aData)), allocate it.
  // This code path is slower than the one used when Allocate has been called
  // since it will trigger a full copy.
  if (!mTextureClient->IsAllocated()) {
    Data data = aData;
    if (!Allocate(data)) {
      NS_WARNING("SharedPlanarYCbCrImage::SetData failed to allocate");
      return;
    }
  }

  MOZ_ASSERT(mTextureClient->AsTextureClientYCbCr());
  if (!mTextureClient->Lock(OPEN_WRITE_ONLY)) {
    MOZ_ASSERT(false, "Failed to lock the texture.");
    return;
  }
  TextureClientAutoUnlock unlock(mTextureClient);
  if (!mTextureClient->AsTextureClientYCbCr()->UpdateYCbCr(aData)) {
    MOZ_ASSERT(false, "Failed to copy YCbCr data into the TextureClient");
    return;
  }
  // do not set mBuffer like in PlanarYCbCrImage because the later
  // will try to manage this memory without knowing it belongs to a
  // shmem.
  mBufferSize = YCbCrImageDataSerializer::ComputeMinBufferSize(mData.mYSize,
                                                               mData.mCbCrSize);
  mSize = mData.mPicSize;

  YCbCrImageDataSerializer serializer(mTextureClient->GetBuffer(), mTextureClient->GetBufferSize());
  mData.mYChannel = serializer.GetYData();
  mData.mCbChannel = serializer.GetCbData();
  mData.mCrChannel = serializer.GetCrData();
  mTextureClient->MarkImmutable();
}

// needs to be overriden because the parent class sets mBuffer which we
// do not want to happen.
uint8_t*
SharedPlanarYCbCrImage::AllocateAndGetNewBuffer(uint32_t aSize)
{
  NS_ABORT_IF_FALSE(!mTextureClient->IsAllocated(), "This image already has allocated data");
  size_t size = YCbCrImageDataSerializer::ComputeMinBufferSize(aSize);

  // get new buffer _without_ setting mBuffer.
  if (!mTextureClient->Allocate(size)) {
    return nullptr;
  }

  // update buffer size
  mBufferSize = size;

  YCbCrImageDataSerializer serializer(mTextureClient->GetBuffer(), mTextureClient->GetBufferSize());
  return serializer.GetData();
}

void
SharedPlanarYCbCrImage::SetDataNoCopy(const Data &aData)
{
  mData = aData;
  mSize = aData.mPicSize;
  /* SetDataNoCopy is used to update YUV plane offsets without (re)allocating
   * memory previously allocated with AllocateAndGetNewBuffer().
   * serializer.GetData() returns the address of the memory previously allocated
   * with AllocateAndGetNewBuffer(), that we subtract from the Y, Cb, Cr
   * channels to compute 0-based offsets to pass to InitializeBufferInfo.
   */
  YCbCrImageDataSerializer serializer(mTextureClient->GetBuffer(), mTextureClient->GetBufferSize());
  uint8_t *base = serializer.GetData();
  uint32_t yOffset = aData.mYChannel - base;
  uint32_t cbOffset = aData.mCbChannel - base;
  uint32_t crOffset = aData.mCrChannel - base;
  serializer.InitializeBufferInfo(yOffset,
                                  cbOffset,
                                  crOffset,
                                  aData.mYStride,
                                  aData.mCbCrStride,
                                  aData.mYSize,
                                  aData.mCbCrSize,
                                  aData.mStereoMode);
}

uint8_t*
SharedPlanarYCbCrImage::AllocateBuffer(uint32_t aSize)
{
  NS_ABORT_IF_FALSE(!mTextureClient->IsAllocated(),
                    "This image already has allocated data");
  if (!mTextureClient->Allocate(aSize)) {
    return nullptr;
  }
  return mTextureClient->GetBuffer();
}

bool
SharedPlanarYCbCrImage::IsValid() {
  return mTextureClient->IsAllocated();
}

bool
SharedPlanarYCbCrImage::Allocate(PlanarYCbCrData& aData)
{
  NS_ABORT_IF_FALSE(!mTextureClient->IsAllocated(),
                    "This image already has allocated data");

  size_t size = YCbCrImageDataSerializer::ComputeMinBufferSize(aData.mYSize,
                                                               aData.mCbCrSize);

  if (AllocateBuffer(static_cast<uint32_t>(size)) == nullptr) {
    return false;
  }

  YCbCrImageDataSerializer serializer(mTextureClient->GetBuffer(), mTextureClient->GetBufferSize());
  serializer.InitializeBufferInfo(aData.mYSize,
                                  aData.mCbCrSize,
                                  aData.mStereoMode);
  MOZ_ASSERT(serializer.IsValid());

  aData.mYChannel = serializer.GetYData();
  aData.mCbChannel = serializer.GetCbData();
  aData.mCrChannel = serializer.GetCrData();

  // copy some of aData's values in mData (most of them)
  mData.mYChannel = aData.mYChannel;
  mData.mCbChannel = aData.mCbChannel;
  mData.mCrChannel = aData.mCrChannel;
  mData.mYSize = aData.mYSize;
  mData.mCbCrSize = aData.mCbCrSize;
  mData.mPicX = aData.mPicX;
  mData.mPicY = aData.mPicY;
  mData.mPicSize = aData.mPicSize;
  mData.mStereoMode = aData.mStereoMode;
  // those members are not always equal to aData's, due to potentially different
  // packing.
  mData.mYSkip = 0;
  mData.mCbSkip = 0;
  mData.mCrSkip = 0;
  mData.mYStride = mData.mYSize.width;
  mData.mCbCrStride = mData.mCbCrSize.width;

  return true;
}

} // namespace
} // namespace
