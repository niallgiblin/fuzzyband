/* ==================================== JUCER_BINARY_RESOURCE ====================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#include <cstring>

namespace BinaryData
{

//================== accompaniment_model.onnx ==================
static const unsigned char temp_binary_data_0[] =
{ 8,10,18,18,77,101,116,97,108,65,99,99,111,109,112,97,110,105,109,101,110,116,58,138,2,10,45,18,3,83,55,105,34,8,67,111,110,115,116,97,110,116,42,28,10,5,118,97,108,117,101,42,16,8,1,16,7,58,1,7,66,7,115,101,118,101,110,95,105,160,1,4,10,44,18,5,97,120,
101,115,49,34,8,67,111,110,115,116,97,110,116,42,25,10,5,118,97,108,117,101,42,13,8,1,16,7,58,1,1,66,4,97,120,101,115,160,1,4,10,44,10,1,88,10,5,97,120,101,115,49,18,4,114,115,117,109,34,9,82,101,100,117,99,101,83,117,109,42,15,10,8,107,101,101,112,100,
105,109,115,24,0,160,1,2,10,31,10,4,114,115,117,109,18,6,114,115,117,109,95,105,34,4,67,97,115,116,42,9,10,2,116,111,24,7,160,1,2,10,34,10,6,114,115,117,109,95,105,10,3,83,55,105,18,1,89,34,3,77,111,100,42,11,10,4,102,109,111,100,24,0,160,1,2,18,18,112,
97,116,116,101,114,110,95,105,110,100,101,120,95,115,116,117,98,90,19,10,1,88,18,14,10,12,8,1,18,8,10,2,8,1,10,2,8,5,98,15,10,1,89,18,10,10,8,8,7,18,4,10,2,8,1,66,4,10,0,16,17,0,0 };

const char* accompaniment_model_onnx = (const char*) temp_binary_data_0;

}

#include "BinaryData.h"

namespace BinaryData
{

const char* getNamedResource (const char* resourceNameUTF8, int& numBytes);
const char* getNamedResource (const char* resourceNameUTF8, int& numBytes)
{
    unsigned int hash = 0;

    if (resourceNameUTF8 != nullptr)
        while (*resourceNameUTF8 != 0)
            hash = 31 * hash + (unsigned int) *resourceNameUTF8++;

    switch (hash)
    {
        case 0x03676b55:  numBytes = 297; return accompaniment_model_onnx;
        case 0xd076c18b:  numBytes = 477; return structure_model_onnx;
        case 0x53060c9f:  numBytes = 599; return bass_model_onnx;
        default: break;
    }

    numBytes = 0;
    return nullptr;
}

const char* namedResourceList[] =
{
    "accompaniment_model_onnx",
    "structure_model_onnx",
    "bass_model_onnx"
};

const char* originalFilenames[] =
{
    "accompaniment_model.onnx",
    "structure_model.onnx",
    "bass_model.onnx"
};

const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8)
{
    for (unsigned int i = 0; i < (sizeof (namedResourceList) / sizeof (namedResourceList[0])); ++i)
        if (strcmp (namedResourceList[i], resourceNameUTF8) == 0)
            return originalFilenames[i];

    return nullptr;
}

}
