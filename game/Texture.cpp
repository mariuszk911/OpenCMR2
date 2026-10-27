#include <stdio.h>
#include <string.h>
#include "Texture.h"
#include "Graphics.h"

// GLOBAL: CMR2 0x00520c98
char g_ddsSuffix[] = ".dds";
// GLOBAL: CMR2 0x00519484
char g_tgaSuffix[] = ".tga";

// Replaces the extension of the file name part of path with the empty string.
// match 86%: below the 90% bar; kept as FUNCTION on purpose so reccmp measures it (see CONVENCIONES)
// FUNCTION: CMR2 0x004a9f50
void Texture_GetPathStem(char *pOut, char *path)
{
    // PORT: AMIGA: accept both separators and truncate the extension directly.
    // GCC 6.5 -O2 miscompiled the old sprintf(dot, "") and left ".tga" on the
    // stem, so the archive was queried as "cmr2.tga.dds" and never found.
    const char *back = strrchr(path, '\\');
    const char *forward = strrchr(path, '/');
    const char *base = path;
    if (back != NULL || forward != NULL) {
        if (back == NULL)
            base = forward + 1;
        else if (forward == NULL)
            base = back + 1;
        else
            base = (forward > back ? forward : back) + 1;
    }
    strcpy(pOut, base);
    char *dot = strrchr(pOut, '.');
    if (dot != NULL)
        *dot = '\0';
}

// Finds a texture in the archive pFile (as .dds, then .tga) and creates it,
// or falls back to loading it from disk by name.
// match 78%: below the 90% bar; kept as FUNCTION on purpose so reccmp measures it (see CONVENCIONES)
// FUNCTION: CMR2 0x004a9e60
Texture* CTexture::FindLoadTexture(GenericFile* pFile, char* textureName, bool *didLoadTexture, DWORD *pSize, bool param5, unsigned int flag) {
    char name[64];
    void *pData;
    Texture *pTexture;

    if (didLoadTexture != NULL)
        *didLoadTexture = false;
    if (pFile != NULL && pFile->didFileLoad && !param5) {
        Texture_GetPathStem(name, textureName);
        sprintf(name + strlen(name), g_ddsSuffix);
        pData = CGenericFileLoader::FindFileInArchive(pFile, name, pSize);
        if (pData == NULL) {
            sprintf(name + strlen(name) - 4, g_tgaSuffix);
            pData = CGenericFileLoader::FindFileInArchive(pFile, name, pSize);
        }
        if (pData != NULL) {
            if (didLoadTexture != NULL)
                *didLoadTexture = true;
            pTexture = CGraphics::CreateTextureFromImageData(textureName, pData, flag);
            // PORT: AMIGA: the loader can reject a staging texture; return
            // the failure instead of dereferencing the NULL result.
            if (pTexture == NULL) {
                if (didLoadTexture != NULL)
                    *didLoadTexture = false;
                return NULL;
            }
            pTexture->pArchive = pFile;
            return pTexture;
        }
    }
    return CGraphics::LoadDDSThenTGATexture(textureName, flag);
}
