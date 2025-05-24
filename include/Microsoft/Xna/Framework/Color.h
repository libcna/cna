//
// Created by robertvokac on 5/24/25.
//

#ifndef COLOR_H
#define COLOR_H


namespace Microsoft::Xna::Framework {

struct Color {
    int r;
int g;
    int b;
    int a;

public:
    Color(int r, int g, int b, int a):
    r(r),
    g(g),
    b(b),
    a(a)
    {

    }

    static Color FromNonPremultiplied(int i, int i1, int i2, int i3);
};
    const Color CornflowerBlue = Color(0,0,0,0);
    const Color White = Color(0,0,0,0);

}



#endif //COLOR_H
