// OpenGL ES 1.1 compatibility layer for the voxel renderer on Android.
// The renderer is written against desktop fixed-function GL with immediate
// mode; ES 1.1 keeps the fixed-function pipeline but drops glBegin/glEnd,
// GL_QUADS, and GLU. This shim provides those on top of client vertex
// arrays. All transforms in the renderer are CPU-side except the
// projection/view matrices, which map directly onto ES equivalents.
#ifndef GUARD_VOXEL_GLES1_COMPAT_H
#define GUARD_VOXEL_GLES1_COMPAT_H

#include <GLES/gl.h>

#define GL_QUADS 0x0007

// ES uses float variants; the renderer passes doubles.
#define glOrtho(l, r, b, t, n, f) \
    glOrthof((GLfloat)(l), (GLfloat)(r), (GLfloat)(b), (GLfloat)(t), (GLfloat)(n), (GLfloat)(f))

void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glTexCoord2f(GLfloat s, GLfloat t);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);

// glColor4f exists in ES 1.1, but the shim must also track the current
// color so it can be captured per-vertex into the color array.
void shimColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
#define glColor4f shimColor4f

void gluPerspective(double fovy, double aspect, double zNear, double zFar);
void gluLookAt(double eyeX, double eyeY, double eyeZ,
               double centerX, double centerY, double centerZ,
               double upX, double upY, double upZ);

#endif // GUARD_VOXEL_GLES1_COMPAT_H
