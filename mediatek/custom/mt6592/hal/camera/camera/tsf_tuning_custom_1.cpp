

#include "camera_custom_types.h"
#include "tsf_tuning_custom.h"

MBOOL
isTSFVdoStop(MINT32 const i4SensorDev)
{
    if (i4SensorDev == 1)
    {
    #ifdef MTK_SLOW_MOTION_VIDEO_SUPPORT
        return MTRUE;
    #else
        return MFALSE;
    #endif
    }
    else
    {
        return MFALSE;
    }
}

