/* The `aspect` setting (port/kz1hd/view_aspect.h): its values, the menu's rows and what each forces. */
#include <math.h>
#include <stdio.h>
#include "view_aspect.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

int main(void)
{
    EXPECT(view_aspect_row("auto") == 0 && view_aspect_value("auto") == 0.0f);
    EXPECT(view_aspect_row(NULL) == 0 && view_aspect_row("") == 0 && view_aspect_row("5:4") == 0 && view_aspect_row("16:9 ") == 0);
    EXPECT(view_aspect_row("4:3") == 1 && fabsf(view_aspect_value("4:3") - 4.0f / 3.0f) < 1e-6f);
    EXPECT(view_aspect_row("16:10") == 2 && view_aspect_value("16:10") == 1.6f);
    EXPECT(view_aspect_row("16:9") == 3 && fabsf(view_aspect_value("16:9") - 16.0f / 9.0f) < 1e-6f);
    EXPECT(view_aspect_row("21:9") == 4 && view_aspect_row("32:9") == 5);
    for (int i = 0; i < VIEW_ASPECTS; i++) EXPECT(view_aspect_row(kViewAspect[i]) == i);
    if (fails) return 1;
    puts("test_view_aspect: ok");
    return 0;
}
