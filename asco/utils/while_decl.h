#ifndef ASCO_WHILE_DECL_H
#define ASCO_WHILE_DECL_H

#define while_decl(decl_cond, body) \
    while (true) if (decl_cond)     \
        body                        \
    else break;

#endif
