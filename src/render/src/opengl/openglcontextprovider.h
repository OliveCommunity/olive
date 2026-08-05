#ifndef OAK_OPENGLCONTEXTPROVIDER_H
#define OAK_OPENGLCONTEXTPROVIDER_H

namespace olive
{

class OpenGLContext;

class OpenGLContextProvider {
public:
	virtual ~OpenGLContextProvider() = default;
	virtual OpenGLContext *open_gl_context() const = 0;
};

}

#endif // OAK_OPENGLCONTEXTPROVIDER_H
