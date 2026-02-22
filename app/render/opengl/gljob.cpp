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

#include "gljob.h"

#include "openglrenderer.h"

namespace olive
{

void GLCreateTextureJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    result_ = renderer->CreateTexture(params_, data_, linesize_);
}

void GLDestroyTextureJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    renderer->DestroyTexture(texture_);
}

void GLUploadTextureJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled() || !texture_) return;
    texture_->Upload(data_, linesize_);
}

void GLDownloadTextureJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled() || !texture_) {
        success_ = false;
        return;
    }
    renderer->DownloadFromTexture(texture_->id(), texture_->params(), data_, linesize_);
    success_ = true;
}

void GLBlitShaderJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    renderer->BlitToTexture(shader_, *job_, destination_.get(), clear_destination_);
}

void GLBlitColorManagedJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    renderer->BlitColorManaged(job_, destination_.get(), params_);
}

void GLClearDestinationJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    renderer->ClearDestination(texture_.get(), r_, g_, b_, a_);
}

void GLCreateShaderJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    result_ = renderer->CreateNativeShader(code_);
}

void GLDestroyShaderJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    renderer->DestroyNativeShader(shader_);
}

void GLFlushJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    renderer->Flush();
}

void GLGetPixelJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled() || !texture_) return;
    result_ = renderer->GetPixelFromTexture(texture_.get(), pt_);
}

void GLInterlaceTextureJob::Execute(OpenGLRenderer *renderer)
{
    if (IsCancelled()) return;
    result_ = renderer->InterlaceTexture(top_, bottom_, params_);
}

} // namespace olive
