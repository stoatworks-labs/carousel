#pragma once

#include <FFGLSDK.h>

namespace carousel
{

/**
    An RGBA32F texture and the framebuffer that draws into it: one
    projector's horizontally blurred slide, at the output's size.

    Its own class rather than `FFGLFBO`, whose `Release` leaks the colour
    texture (the fleet's trap) and whose float path is half-float on some
    hosts: the blur carries linear-light transmittance that the screen pass
    weights again, and every claim about it is made in float.

    Read with `texelFetch` only, so the filters are NEAREST (an incomplete
    texture fetches nothing useful, OpenGL 4.1 core 3.8.14). RGBA32F is a
    required colour-renderable format (3.8.3).

    `Ensure` reallocates only when the size changes, and puts back the
    texture and framebuffer bindings it touched. Call it before anything is
    bound for the frame.
*/
class BlurBuffer
{
public:
	BlurBuffer() = default;
	~BlurBuffer() = default;
	BlurBuffer( const BlurBuffer& ) = delete;
	BlurBuffer& operator=( const BlurBuffer& ) = delete;

	/// Make it `width` x `height`, keeping it if it already is. False if the
	/// GL would not make it, with nothing left allocated.
	bool Ensure( int width, int height );

	/// Free both GL objects. Safe to call twice.
	void Destroy();

	GLuint Texture() const
	{
		return texture;
	}
	GLuint Framebuffer() const
	{
		return fbo;
	}
	int Width() const
	{
		return width;
	}
	int Height() const
	{
		return height;
	}
	bool Ready() const
	{
		return texture != 0 && fbo != 0;
	}

private:
	GLuint texture = 0;
	GLuint fbo     = 0;
	int width      = 0;
	int height     = 0;
};

} // namespace carousel
