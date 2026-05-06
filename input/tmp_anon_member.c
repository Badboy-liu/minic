// 匿名结构体成员测试
struct Inner {
    int x;
    int y;
};

struct Outer {
    struct Inner;  // 匿名成员
    int z;
};

int main() {
    struct Outer o;
    o.x = 10;
    o.y = 20;
    o.z = 30;
    return o.x + o.y + o.z;  // 60
}
