#pragma once
#include "mcc2.h"

Ident* declare_ident(Token* ident, IdentKind kind, Type* ty);
Ident* register_string_literal(Token* tok);
Ident* find_ident(Token* tok);
void scope_in();
void scope_out();
int get_stack_size();
Scope* get_global_scope();
