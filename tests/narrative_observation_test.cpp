#include "features/narrative/observation.h"
#include <cstdlib>
#include <initializer_list>
using namespace nx::features::narrative;
void check(bool value) { if (!value) std::abort(); }
int main() {
    Context c;
    check(evaluate(c).state == Eligibility::unknown);
    c.build = c.session = c.language = c.selected = c.smalltalk = c.identity = Known::yes;
    // Even identified, selected chatter says nothing about pending obligations.
    check(evaluate(c).state == Eligibility::unknown);
    c.npc_pending = Known::no;
    check(evaluate(c).state == Eligibility::unknown);
    c.island_pending = Known::yes;
    check(evaluate(c).state == Eligibility::blocked);
    c.identity = Known::unknown;
    check(evaluate(c).state == Eligibility::blocked);
    c.island_pending = Known::no;
    check(evaluate(c).state == Eligibility::unknown);
    c.identity = Known::yes;
    check(evaluate(c).state == Eligibility::eligible);
    c.busy = true;
    check(evaluate(c).state == Eligibility::blocked);
    c.busy = false;
    // Every required positive fact must independently gate eligibility.
    for (auto field : {&Context::build, &Context::session, &Context::language,
                       &Context::selected, &Context::smalltalk, &Context::identity}) {
        c.*field = Known::unknown;
        check(evaluate(c).state == Eligibility::unknown);
        c.*field = Known::no;
        check(evaluate(c).state == Eligibility::blocked);
        c.*field = Known::yes;
    }
    c.npc_pending = Known::yes;
    check(evaluate(c).reasons & local_pending);
    check(evaluate(c).state == Eligibility::blocked);
}
