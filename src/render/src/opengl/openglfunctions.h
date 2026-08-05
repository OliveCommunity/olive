/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_OPENGLFUNCTIONS_H
#define OAK_OPENGLFUNCTIONS_H

// De-Qt replacement for QOpenGLFunctions/QOpenGLExtraFunctions.
//
// GL types/enums come from the system GL headers; entry points are stored as
// function pointer members so existing call sites of the form
// `functions_->glFoo(...)` keep their exact shape. On Apple the pointers are
// bound directly to the OpenGL framework symbols; elsewhere they are resolved
// through the platform get-proc-address hook supplied by OpenGLContext.

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace olive
{

struct OpenGLFunctions {
	GLenum (*glGetError)();
	void (*glGenTextures)(GLsizei, GLuint *);
	void (*glDeleteTextures)(GLsizei, const GLuint *);
	GLboolean (*glIsTexture)(GLuint);
	void (*glPixelStorei)(GLenum, GLint);
	void (*glGetIntegerv)(GLenum, GLint *);
	void (*glBindTexture)(GLenum, GLuint);
	void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
						 GLenum, const void *);
	void (*glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint,
						 GLenum, GLenum, const void *);
	void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei,
							GLenum, GLenum, const void *);
	void (*glTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei,
							GLsizei, GLsizei, GLenum, GLenum, const void *);
	void (*glTexParameteri)(GLenum, GLenum, GLint);
	void (*glGenerateMipmap)(GLenum);
	GLuint (*glCreateProgram)();
	void (*glAttachShader)(GLuint, GLuint);
	void (*glLinkProgram)(GLuint);
	void (*glGetProgramiv)(GLuint, GLenum, GLint *);
	void (*glDeleteProgram)(GLuint);
	GLuint (*glCreateShader)(GLenum);
	void (*glShaderSource)(GLuint, GLsizei, const char *const *, const GLint *);
	void (*glCompileShader)(GLuint);
	void (*glGetShaderiv)(GLuint, GLenum, GLint *);
	void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, char *);
	void (*glDeleteShader)(GLuint);
	void (*glUseProgram)(GLuint);
	GLint (*glGetUniformLocation)(GLuint, const char *);
	void (*glUniform1i)(GLint, GLint);
	void (*glUniform1f)(GLint, GLfloat);
	void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
	void (*glUniform2fv)(GLint, GLsizei, const GLfloat *);
	void (*glUniform3fv)(GLint, GLsizei, const GLfloat *);
	void (*glUniform4fv)(GLint, GLsizei, const GLfloat *);
	void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
	void (*glActiveTexture)(GLenum);
	void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
	GLint (*glGetAttribLocation)(GLuint, const char *);
	void (*glEnableVertexAttribArray)(GLuint);
	void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
								  const void *);
	void (*glDrawArrays)(GLenum, GLint, GLsizei);
	void (*glGenFramebuffers)(GLsizei, GLuint *);
	void (*glDeleteFramebuffers)(GLsizei, const GLuint *);
	void (*glBindFramebuffer)(GLenum, GLuint);
	void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
	GLenum (*glCheckFramebufferStatus)(GLenum);
	void (*glFinish)();
	void (*glFlush)();
	void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
						 void *);
	void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
	void (*glClear)(GLbitfield);
	void (*glGenVertexArrays)(GLsizei, GLuint *);
	void (*glDeleteVertexArrays)(GLsizei, const GLuint *);
	void (*glBindVertexArray)(GLuint);
	void (*glGenBuffers)(GLsizei, GLuint *);
	void (*glDeleteBuffers)(GLsizei, const GLuint *);
	void (*glBindBuffer)(GLenum, GLuint);
	void (*glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
};

// Resolves every entry point. When `get_proc` is null (Apple), pointers are
// bound directly to the linked OpenGL framework symbols; otherwise each entry
// is fetched through the platform get-proc-address callback. Returns false if
// any entry point could not be resolved.
bool resolve_open_gl_functions(OpenGLFunctions *functions,
							   void *(*get_proc)(const char *name));

}

#endif // OAK_OPENGLFUNCTIONS_H
