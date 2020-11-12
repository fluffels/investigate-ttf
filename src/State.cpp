#include "State.h"

#include "MathLib.cpp"

void eventMoveForward(float d, Uniforms& u) {
    moveAlongQuaternion(d, u.rotation, u.eye);
}

void eventMoveBackward(float d, Uniforms& u) {
    moveAlongQuaternion(-d, u.rotation, u.eye);
}

void eventMoveLeft(float d, Uniforms& u) {
    movePerpendicularToQuaternion(-d, u.rotation, u.eye);
}

void eventMoveRight(float d, Uniforms& u) {
    movePerpendicularToQuaternion(d, u.rotation, u.eye);
}

void eventRotateX(float d, Uniforms& u) {
    rotateQuaternionX(d, u.rotation);
}

void eventRotateY(float d, Uniforms& u) {
    rotateQuaternionY(-d, u.rotation);
}

void eventPositionReset(Uniforms& u) {
    u.eye = { 0, 0.f, -1.f, 0 };
}
