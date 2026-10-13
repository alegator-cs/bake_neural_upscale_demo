#version 450
layout(location=0) in vec2 bary12;
layout(location=1) flat in uint triangleID;
layout(location=0) out uint hitTriangle;
layout(location=1) out vec2 hitBarycentrics;
void main() {
    hitTriangle=triangleID;
    hitBarycentrics=bary12;
}
