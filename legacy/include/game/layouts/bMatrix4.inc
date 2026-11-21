// complete
bVector4 v0;
bVector4 v1;
bVector4 v2;
bVector4 v3;
bMatrix4() {}
bVector4 &operator[](int row) { return (&v0)[row]; }
const bVector4 &operator[](int row) const { return (&v0)[row]; }
