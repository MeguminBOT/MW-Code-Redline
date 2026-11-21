// complete
float x;
float y;
float z;
float pad;
bVector3() {}
bVector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
bVector3 operator*(float s) const { return bVector3(x * s, y * s, z * s); }
bVector3 operator-(const bVector3 &v) const { return bVector3(x - v.x, y - v.y, z - v.z); }
bVector3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
