/* CLSID of the Windows resampler DMO used by RtAudio's WASAPI back end. MinGW has no
   wmcodecdsp import library, so the GUID is defined here. */
#include <guiddef.h>
const GUID CLSID_CResamplerMediaObject = { 0xf447b69e, 0x1884, 0x4a7e, { 0x80, 0x55, 0x34, 0x6f, 0x74, 0xd6, 0xed, 0xb3 } };
