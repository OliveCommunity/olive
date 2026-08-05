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

#include "openglfunctions.h"

namespace olive
{

bool resolve_open_gl_functions(OpenGLFunctions *f,
							   void *(*get_proc)(const char *))
{
	if (!f) {
		return false;
	}

#if defined(__APPLE__)
	// The OpenGL framework exports every GL 3.x core entry point as a real
	// symbol, so bind directly instead of going through a loader.
	(void) get_proc;
	f->glGetError = &::glGetError;
	f->glGenTextures = &::glGenTextures;
	f->glDeleteTextures = &::glDeleteTextures;
	f->glIsTexture = &::glIsTexture;
	f->glPixelStorei = &::glPixelStorei;
	f->glGetIntegerv = &::glGetIntegerv;
	f->glBindTexture = &::glBindTexture;
	f->glTexImage2D = &::glTexImage2D;
	f->glTexImage3D = &::glTexImage3D;
	f->glTexSubImage2D = &::glTexSubImage2D;
	f->glTexSubImage3D = &::glTexSubImage3D;
	f->glTexParameteri = &::glTexParameteri;
	f->glGenerateMipmap = &::glGenerateMipmap;
	f->glCreateProgram = &::glCreateProgram;
	f->glAttachShader = &::glAttachShader;
	f->glLinkProgram = &::glLinkProgram;
	f->glGetProgramiv = &::glGetProgramiv;
	f->glDeleteProgram = &::glDeleteProgram;
	f->glCreateShader = &::glCreateShader;
	f->glShaderSource = &::glShaderSource;
	f->glCompileShader = &::glCompileShader;
	f->glGetShaderiv = &::glGetShaderiv;
	f->glGetShaderInfoLog = &::glGetShaderInfoLog;
	f->glDeleteShader = &::glDeleteShader;
	f->glUseProgram = &::glUseProgram;
	f->glGetUniformLocation = &::glGetUniformLocation;
	f->glUniform1i = &::glUniform1i;
	f->glUniform1f = &::glUniform1f;
	f->glUniform4f = &::glUniform4f;
	f->glUniform2fv = &::glUniform2fv;
	f->glUniform3fv = &::glUniform3fv;
	f->glUniform4fv = &::glUniform4fv;
	f->glUniformMatrix4fv = &::glUniformMatrix4fv;
	f->glActiveTexture = &::glActiveTexture;
	f->glViewport = &::glViewport;
	f->glGetAttribLocation = &::glGetAttribLocation;
	f->glEnableVertexAttribArray = &::glEnableVertexAttribArray;
	f->glVertexAttribPointer = &::glVertexAttribPointer;
	f->glDrawArrays = &::glDrawArrays;
	f->glGenFramebuffers = &::glGenFramebuffers;
	f->glDeleteFramebuffers = &::glDeleteFramebuffers;
	f->glBindFramebuffer = &::glBindFramebuffer;
	f->glFramebufferTexture2D = &::glFramebufferTexture2D;
	f->glCheckFramebufferStatus = &::glCheckFramebufferStatus;
	f->glFinish = &::glFinish;
	f->glFlush = &::glFlush;
	f->glReadPixels = &::glReadPixels;
	f->glClearColor = &::glClearColor;
	f->glClear = &::glClear;
	f->glGenVertexArrays = &::glGenVertexArrays;
	f->glDeleteVertexArrays = &::glDeleteVertexArrays;
	f->glBindVertexArray = &::glBindVertexArray;
	f->glGenBuffers = &::glGenBuffers;
	f->glDeleteBuffers = &::glDeleteBuffers;
	f->glBindBuffer = &::glBindBuffer;
	f->glBufferData = &::glBufferData;
	return true;
#else
	if (!get_proc) {
		return false;
	}
	bool ok = true;
#define OAK_GL_RESOLVE(member)                                                \
	f->member = reinterpret_cast<decltype(f->member)>(get_proc(#member));     \
	ok = ok && (f->member != nullptr)
	OAK_GL_RESOLVE(glGetError);
	OAK_GL_RESOLVE(glGenTextures);
	OAK_GL_RESOLVE(glDeleteTextures);
	OAK_GL_RESOLVE(glIsTexture);
	OAK_GL_RESOLVE(glPixelStorei);
	OAK_GL_RESOLVE(glGetIntegerv);
	OAK_GL_RESOLVE(glBindTexture);
	OAK_GL_RESOLVE(glTexImage2D);
	OAK_GL_RESOLVE(glTexImage3D);
	OAK_GL_RESOLVE(glTexSubImage2D);
	OAK_GL_RESOLVE(glTexSubImage3D);
	OAK_GL_RESOLVE(glTexParameteri);
	OAK_GL_RESOLVE(glGenerateMipmap);
	OAK_GL_RESOLVE(glCreateProgram);
	OAK_GL_RESOLVE(glAttachShader);
	OAK_GL_RESOLVE(glLinkProgram);
	OAK_GL_RESOLVE(glGetProgramiv);
	OAK_GL_RESOLVE(glDeleteProgram);
	OAK_GL_RESOLVE(glCreateShader);
	OAK_GL_RESOLVE(glShaderSource);
	OAK_GL_RESOLVE(glCompileShader);
	OAK_GL_RESOLVE(glGetShaderiv);
	OAK_GL_RESOLVE(glGetShaderInfoLog);
	OAK_GL_RESOLVE(glDeleteShader);
	OAK_GL_RESOLVE(glUseProgram);
	OAK_GL_RESOLVE(glGetUniformLocation);
	OAK_GL_RESOLVE(glUniform1i);
	OAK_GL_RESOLVE(glUniform1f);
	OAK_GL_RESOLVE(glUniform4f);
	OAK_GL_RESOLVE(glUniform2fv);
	OAK_GL_RESOLVE(glUniform3fv);
	OAK_GL_RESOLVE(glUniform4fv);
	OAK_GL_RESOLVE(glUniformMatrix4fv);
	OAK_GL_RESOLVE(glActiveTexture);
	OAK_GL_RESOLVE(glViewport);
	OAK_GL_RESOLVE(glGetAttribLocation);
	OAK_GL_RESOLVE(glEnableVertexAttribArray);
	OAK_GL_RESOLVE(glVertexAttribPointer);
	OAK_GL_RESOLVE(glDrawArrays);
	OAK_GL_RESOLVE(glGenFramebuffers);
	OAK_GL_RESOLVE(glDeleteFramebuffers);
	OAK_GL_RESOLVE(glBindFramebuffer);
	OAK_GL_RESOLVE(glFramebufferTexture2D);
	OAK_GL_RESOLVE(glCheckFramebufferStatus);
	OAK_GL_RESOLVE(glFinish);
	OAK_GL_RESOLVE(glFlush);
	OAK_GL_RESOLVE(glReadPixels);
	OAK_GL_RESOLVE(glClearColor);
	OAK_GL_RESOLVE(glClear);
	OAK_GL_RESOLVE(glGenVertexArrays);
	OAK_GL_RESOLVE(glDeleteVertexArrays);
	OAK_GL_RESOLVE(glBindVertexArray);
	OAK_GL_RESOLVE(glGenBuffers);
	OAK_GL_RESOLVE(glDeleteBuffers);
	OAK_GL_RESOLVE(glBindBuffer);
	OAK_GL_RESOLVE(glBufferData);
#undef OAK_GL_RESOLVE
	return ok;
#endif
}

}
