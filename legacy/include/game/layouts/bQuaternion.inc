// complete
float x;
float y;
float z;
float w;
bQuaternion() {}
bQuaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
bQuaternion &Slerp(bQuaternion &result, const bQuaternion &to, float t) const;
