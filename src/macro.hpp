#ifndef value_or
#define value_or(var, expr)                  \
    auto var##_open_result = expr;           \
    if(!var##_open_result) {                 \
        return var##_open_result.as_error(); \
    }                                        \
    auto& var = var##_open_result.as_value();
#endif

#ifndef error_or
#define error_or(c)        \
    if(const auto e = c) { \
        return e;          \
    }
#endif
