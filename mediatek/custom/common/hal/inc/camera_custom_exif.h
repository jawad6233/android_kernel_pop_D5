#ifndef _CAMERA_CUSTOM_EXIF_
#define _CAMERA_CUSTOM_EXIF_
//
#include "camera_custom_types.h"

//
namespace NSCamCustom
{
typedef struct SensorExifInfo_S
{
    MUINT32 uFLengthNum;
    MUINT32 uFLengthDenom;
    
} SensorExifInfo_T;

SensorExifInfo_T const&
getParamSensorExif()
{
    static SensorExifInfo_T inst = { 
        uFLengthNum     : 35, // Numerator of Focal Length. Default is 35.
        uFLengthDenom   : 10, // Denominator of Focal Length, it should not be 0.  Default is 10.
    };
    return inst;
}


//#define EN_CUSTOM_EXIF_INFO
#define EN_CUSTOM_EXIF_INFO//modified by hongqi.tian.hz for PR625731
#define SET_EXIF_TAG_STRING(tag,str) \
    if (strlen((const char*)str) <= 32) { \
        strcpy((char *)pexifApp1Info->tag, (const char*)str); }
        
typedef struct customExifInfo_s {
    unsigned char strMake[32];
    unsigned char strModel[32];
    unsigned char strSoftware[32];
} customExifInfo_t;

MINT32 custom_SetExif(void **ppCustomExifTag)
{
// 625731 [Camera][Gallery]Marker and model not display in details.  modified by hongqi.tian.hz at0321 begin
#ifdef EN_CUSTOM_EXIF_INFO
//#define CUSTOM_EXIF_STRING_MAKE  "custom make"
//#define CUSTOM_EXIF_STRING_MODEL "custom model"
//#define CUSTOM_EXIF_STRING_SOFTWARE "custom software"
//static customExifInfo_t exifTag = {CUSTOM_EXIF_STRING_MAKE,CUSTOM_EXIF_STRING_MODEL,CUSTOM_EXIF_STRING_SOFTWARE};    char model[32];
    char model[PROPERTY_VALUE_MAX];
    char manufacturer[PROPERTY_VALUE_MAX];
    property_get("ro.product.model", model, "default");
    ALOGI("custom_SetExif model= %s",model);
    property_get("ro.product.manufacturer", manufacturer, "default");
    ALOGI("custom_SetExif manufacturer= %s",manufacturer);
    //#define CUSTOM_EXIF_STRING_MAKE  "custom make"
    //#define CUSTOM_EXIF_STRING_MODEL "custom model"
    static customExifInfo_t exifTag = { 0 };
    for (int i = 0; i < 32; i++) {
        if (model[i] != '\0' && i < strlen(model)) {
            exifTag.strModel[i] = (unsigned char) model[i];
        } else {
            exifTag.strModel[i] = '\0';
        }
        if (manufacturer[i] != '\0' && i < strlen(manufacturer)) {
            exifTag.strMake[i] = (unsigned char) manufacturer[i];
        } else {
            exifTag.strMake[i] = '\0';
        }
    }
// 625731 [Camera][Gallery]Marker and model not display in details.  modified by hongqi.tian.hz at0321 end
    if (0 != ppCustomExifTag) {
        *ppCustomExifTag = (void*)&exifTag;
    }
    return 0;
#else
    return -1;
#endif
}


typedef struct customExif_s
{
    MBOOL   bEnCustom;
    MUINT32 u4ExpProgram;
    
} customExif_t;

customExif_t const&
getCustomExif()
{
    static customExif_t inst = {
        bEnCustom       :   false,  // default value: false.
        u4ExpProgram    :   0,      // default value: 0.    '0' means not defined, '1' manual control, '2' program normal
    };
    return inst;
}


};  //NSCamCustom
#endif  //  _CAMERA_CUSTOM_EXIF_

