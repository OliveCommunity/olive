#ifndef OAK_OPENGLCONTEXTPROVIDER_H
#define OAK_OPENGLCONTEXTPROVIDER_H

class QOpenGLContext;

namespace olive
{

class OpenGLContextProvider {
public:
	virtual ~OpenGLContextProvider() = default;
	virtual QOpenGLContext *open_gl_context() const = 0;
};

}

#endif // OAK_OPENGLCONTEXTPROVIDER_H
