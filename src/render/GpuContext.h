#pragma once

#include <memory>

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QRhi;
QT_END_NAMESPACE

namespace hyperwave {

// One QRhi for the whole process.
//
// A QRhi owns a Metal device and a command queue, and there are now two things
// that want one -- the strands renderer and the bloom pass -- with a third
// arriving the moment any other look goes to the GPU. Two devices for a menu
// bar item would be silly, and worse, resources created on one cannot be used
// by the other, which is the kind of thing that fails at the point where the
// two are finally combined rather than where the mistake was made.
//
// Held weakly, so it goes away when the last user does. Switching from the
// strands back to the bars should not leave a GPU context alive for the rest of
// the session.
namespace GpuContext {

std::shared_ptr<QRhi> acquire();

// Whether a GPU is usable at all. Answered once and remembered: a machine that
// cannot create a QRhi will not start being able to.
bool isAvailable();

} // namespace GpuContext

} // namespace hyperwave
