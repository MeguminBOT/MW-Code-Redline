// complete
float x;
float y;
float z;
float w;
bVector4() {}
bVector4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
bVector4 operator+(const bVector4 &v) { return bVector4(x + v.x, y + v.y, z + v.z, w + v.w); }
bVector4 &operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
float &operator[](int i) { return (&x)[i]; }
const float &operator[](int i) const { return (&x)[i]; }
