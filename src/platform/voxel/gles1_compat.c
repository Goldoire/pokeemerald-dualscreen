#if defined(PLATFORM_SDL2) && defined(__ANDROID__)
// Immediate-mode-over-vertex-arrays shim for GLES 1.1; see gles1_compat.h.
#include <math.h>
#include <string.h>
#include "gles1_compat.h"
#undef glColor4f // call the real ES entry point from here

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Enough for the biggest mesh batches; flushed at quad boundaries on overflow.
#define SHIM_MAX_VERTS 16384

static GLenum sMode;
static int sVertCount;
static GLfloat sCurColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static GLfloat sCurTexCoord[2];
static GLfloat sPositions[SHIM_MAX_VERTS * 3];
static GLfloat sTexCoords[SHIM_MAX_VERTS * 2];
static GLfloat sColors[SHIM_MAX_VERTS * 4];
// Quads are expanded to triangles into these buffers at glEnd.
static GLfloat sTriPositions[SHIM_MAX_VERTS * 3 * 6 / 4];
static GLfloat sTriTexCoords[SHIM_MAX_VERTS * 2 * 6 / 4];
static GLfloat sTriColors[SHIM_MAX_VERTS * 4 * 6 / 4];

void glBegin(GLenum mode)
{
    sMode = mode;
    sVertCount = 0;
}

static void CopyVertex(GLfloat *dstPos, GLfloat *dstUv, GLfloat *dstCol, int dst, int src)
{
    memcpy(&dstPos[dst * 3], &sPositions[src * 3], 3 * sizeof(GLfloat));
    memcpy(&dstUv[dst * 2], &sTexCoords[src * 2], 2 * sizeof(GLfloat));
    memcpy(&dstCol[dst * 4], &sColors[src * 4], 4 * sizeof(GLfloat));
}

static void Flush(void)
{
    const GLfloat *positions = sPositions;
    const GLfloat *texCoords = sTexCoords;
    const GLfloat *colors = sColors;
    int drawCount = sVertCount;

    if (sVertCount == 0)
        return;

    if (sMode == GL_QUADS)
    {
        int quads = sVertCount / 4;
        int q;
        for (q = 0; q < quads; q++)
        {
            static const int corner[6] = {0, 1, 2, 0, 2, 3};
            int i;
            for (i = 0; i < 6; i++)
                CopyVertex(sTriPositions, sTriTexCoords, sTriColors,
                           q * 6 + i, q * 4 + corner[i]);
        }
        positions = sTriPositions;
        texCoords = sTriTexCoords;
        colors = sTriColors;
        drawCount = quads * 6;
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, positions);
    glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
    glColorPointer(4, GL_FLOAT, 0, colors);
    glDrawArrays(sMode == GL_QUADS ? GL_TRIANGLES : sMode, 0, drawCount);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    sVertCount = 0;
}

void glEnd(void)
{
    Flush();
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    // Flush full buffers only between primitives.
    if (sVertCount >= SHIM_MAX_VERTS - 4
     && (sMode != GL_QUADS || (sVertCount & 3) == 0))
        Flush();

    sPositions[sVertCount * 3 + 0] = x;
    sPositions[sVertCount * 3 + 1] = y;
    sPositions[sVertCount * 3 + 2] = z;
    memcpy(&sTexCoords[sVertCount * 2], sCurTexCoord, sizeof(sCurTexCoord));
    memcpy(&sColors[sVertCount * 4], sCurColor, sizeof(sCurColor));
    sVertCount++;
}

void glVertex2f(GLfloat x, GLfloat y)
{
    glVertex3f(x, y, 0.0f);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    sCurTexCoord[0] = s;
    sCurTexCoord[1] = t;
}

void shimColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    sCurColor[0] = r;
    sCurColor[1] = g;
    sCurColor[2] = b;
    sCurColor[3] = a;
    glColor4f(r, g, b, a);
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    shimColor4f(r, g, b, 1.0f);
}

void gluPerspective(double fovy, double aspect, double zNear, double zFar)
{
    GLfloat m[16];
    float f = 1.0f / tanf((float)(fovy * M_PI / 360.0));
    float nf = (float)(1.0 / (zNear - zFar));

    memset(m, 0, sizeof(m));
    m[0] = f / (float)aspect;
    m[5] = f;
    m[10] = (float)((zFar + zNear) * nf);
    m[11] = -1.0f;
    m[14] = (float)(2.0 * zFar * zNear * nf);
    glMultMatrixf(m);
}

void gluLookAt(double eyeX, double eyeY, double eyeZ,
               double centerX, double centerY, double centerZ,
               double upX, double upY, double upZ)
{
    float fx = (float)(centerX - eyeX);
    float fy = (float)(centerY - eyeY);
    float fz = (float)(centerZ - eyeZ);
    float len = sqrtf(fx * fx + fy * fy + fz * fz);
    float sx, sy, sz, ux, uy, uz, slen;
    GLfloat m[16];

    if (len != 0.0f) { fx /= len; fy /= len; fz /= len; }

    sx = fy * (float)upZ - fz * (float)upY;
    sy = fz * (float)upX - fx * (float)upZ;
    sz = fx * (float)upY - fy * (float)upX;
    slen = sqrtf(sx * sx + sy * sy + sz * sz);
    if (slen != 0.0f) { sx /= slen; sy /= slen; sz /= slen; }

    ux = sy * fz - sz * fy;
    uy = sz * fx - sx * fz;
    uz = sx * fy - sy * fx;

    memset(m, 0, sizeof(m));
    m[0] = sx; m[4] = sy; m[8] = sz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[15] = 1.0f;
    glMultMatrixf(m);
    glTranslatef((float)-eyeX, (float)-eyeY, (float)-eyeZ);
}

#endif // PLATFORM_SDL2 && __ANDROID__
