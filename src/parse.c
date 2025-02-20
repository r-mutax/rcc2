#include "mcc2.h"

/*
    program = ( function | declaration )*
    function = ident '(' (declspec ident)*'){' compound_stmt
    stmt = expr ';' |
            'return' expr ';' |
            'if(' expr ')' stmt ('else' stmt)? |
            'while(' expr ')' stmt |
            'for(' (expr | declspec ident ('=' assign) ? )? ';' expr? '; expr? )' stmt |
            '{' compound_stmt |
            'switch(' expr ')' stmt |
            'case' const_expr ':' |
            'default:' |
            'goto' ident ';'
    compound_stmt = stmt* | declaration* '}'
    declaration = declare '=' (expr)? ';'
    declare = declspec ident
    declspec = 'int' '*' * | 'char' '*' * | 'short' '*' * | 'struct' ident | 'union' ident
    expr = assign (',' assign)*
    assign = cond_expr ( '=' assign
                        | '+=' assign
                        | '-=' assign
                        | '*=' assign
                        | '/=' assign
                        | '%=' assign
                        | '<<' assign
                        | '>>' assign
                    )?
    cond_expr = logicOR ( '?' expr : cond_expr );
    logicOr = logicAnd ( '||' logicOr )*
    logicAnd = bitOr ( '&&' bitOr )*
    bitOr = bitXor ('|' bitXor)*
    bitXor = bitAnd ('^' bitAnd )*
    bitAnd = equality ('&' equality )*
    equality = relational ('==' relational | '!=' relational)*
    relational = bitShift ('<' bitShift | '<=' bitShift | '>' bitShift | '>=' bitShift)*
    bitShift = add ('<<' add | '>>' add)?
    add = mul ('+' mul | '-' mul)*
    mul = unary ('*' cast | '/' cast | '%' cast)*
    cast = '(' type-name ')' cast | unary
    unary = ('+' | '-' | '&' unary | '*' unary | 'sizeof' unary )? postfix
    postfix = primary ('[' expr ']')*
    primary = '(' expr ')' | num | ident | ident '()'
*/

static Node* switch_node = NULL;
static Type* cur_func_type = NULL;
static Token* token = NULL;

static void Program();
static void function(Type* ty, StorageClassKind sck);
static Node* stmt();
static Node* compound_stmt();
static Node* declaration(Type* ty, StorageClassKind sck);
static Ident* declare(Type* ty, StorageClassKind sck);
static Type* declspec(StorageClassKind* sck);
static bool check_storage_class_keyword(StorageClassKind* sck, Token* tok);
static void count_decl_spec(int* type_flg, int flg, Token* tok);
static Member* struct_or_union_member();
static Type* struct_or_union_spec(bool is_union);
static Type* enum_spec();
static Member* enum_member();
static Node* exchange_constant_expr(Node* expr);
static Node* expr();
static Node* assign();
static Node* cond_expr();
static Node* logicOr();
static Node* logicAnd();
static Node* bitOr();
static Node* bitXor();
static Node* bitAnd();
static Node* equality();
static Node* relational();
static Node* bitShift();
static Node* add();
static Node* mul();
static Node* cast();
static Node* unary();
static Node* postfix();
static Node* primary();
static Node* const_expr();
static Node* new_node(NodeKind kind, Node* lhs, Node* rhs);
static Node* new_node_add(Node* lhs, Node* rhs);
static Node* new_node_sub(Node* lhs, Node* rhs);
static Node* new_node_div(Node* lhs, Node* rhs);
static Node* new_node_mul(Node* lhs, Node* rhs);
static Node* new_node_mod(Node* lhs, Node* rhs);
static Node* new_node_num(unsigned long num);
static Node* new_node_var(Ident* ident);
static Node* new_inc(Node* var);
static Node* new_dec(Node* var);
static bool is_function();

// ----------------------------------------
// トークン操作
void expect_token(TokenKind kind);
bool consume_token(TokenKind kind);
Token* consume_ident();
Token* expect_ident();
Token* consume_string_literal();
Token* consume_typedef_name();
unsigned long expect_num();
bool is_eof();
bool is_type();
bool is_label();
Token* get_token();
void set_token(Token* tok);

// ビルトインのトークン定義
Token unnamed_struct_token = MAKE_TOKEN(TK_IDENT, "__unnamed_struct");
Token unnamed_enum_token = MAKE_TOKEN(TK_IDENT, "__unnamed_enum");
Token va_arena_token = MAKE_TOKEN(TK_IDENT, "__va_area__");
Token builtin_va_elem_token = MAKE_TOKEN(TK_IDENT, "__builtin_va_elem");
Token spill_area_token = MAKE_TOKEN(TK_IDENT, "__spill_area__");
Token va_elem_gp_offset_token = MAKE_TOKEN(TK_IDENT, "gp_offset");
Token va_elem_fp_offset_token = MAKE_TOKEN(TK_IDENT, "fp_offset");
Token va_elem_overflow_arg_area_token = MAKE_TOKEN(TK_IDENT, "overflow_arg_area");
Token va_elem_reg_save_area_token = MAKE_TOKEN(TK_IDENT, "reg_save_area");
Token tmp_token = MAKE_TOKEN(TK_IDENT, "__tmp__");

#define VA_AREA_SIZE 24 + 8 * 6 + 8 * 8

void parse(Token* tok){
    token = tok;
    Program();
    return;
}

void Program(){
    while(!is_eof()){
        StorageClassKind sck = SCK_NONE;
        Type* ty = declspec(&sck);

        if(consume_token(TK_SEMICORON)){
            continue;
        }

        if(is_function()){
            function(ty, sck);
        } else {
            declaration(ty, sck);
        }
    }

    return;
}

static void function(Type* func_type, StorageClassKind sck){

    while(consume_token(TK_MUL)){
        func_type = pointer_to(func_type);
    }

    Token* tok = consume_ident();
    if(!tok){
        // 識別子がない場合は、関数宣言がない
        return;
    }

    // 前方宣言があるか検索
    Ident* func = find_ident(tok);
    bool has_forward_def = false;       // 前方宣言あるか？（パラメータ個数チェック用）
    if(!func){
        func = declare_ident(tok, ID_FUNC, func_type);
    } else {
        // ある場合は戻り値型がconflictしてないかチェック
        if(!equal_type(func_type, func->type)){
            error_tok(tok, "conflict definition type.");
        }
        has_forward_def = true;
    }

    // ここでスコープインして、仮引数は関数スコープ内で宣言するため、ローカル変数と同等に扱える。
    scope_in();
    expect_token(TK_L_PAREN);

    if(!consume_token(TK_R_PAREN)){
        Parameter head = {};
        Parameter* cur = &head;
        do {
            if(consume_token(TK_DOT_DOT_DOT)){
                func->is_var_params = true;
                break;
            } else {
                StorageClassKind sck = 0;
                Type* ty = declspec(&sck);
                Ident* ident = declare(ty, sck);
                register_ident(ident);
                Parameter* param = calloc(1, sizeof(Parameter));
                param->ident = ident;
                cur = cur->next = param;
            }
        } while(consume_token(TK_COMMA));
        func->params = head.next;
        expect_token(TK_R_PAREN);
    }

    func->scope = get_current_scope();

    if(consume_token(TK_SEMICORON)){
        // ここでセミコロンがあるなら前方宣言
        scope_out();
        return;
    } else {
        if(has_forward_def && func->funcbody){
            // すでに定義された関数のbodyがあるのに、ここでも定義している
            error_tok(tok, "Conflict function definition.");
        }
    }

    // 実レジスタ退避用の領域を確保(とりあえず30個確保する)
    Ident* spill_area = make_ident(&spill_area_token, ID_LVAR, ty_char);
    spill_area->type = array_of(ty_char, 8 * 30);
    register_ident(spill_area);

    // ここまで来たら関数の定義
    // 可変長引数ありの関数の場合は、__va_area__を宣言
    if(func->is_var_params){
        Ident* va_area = make_ident(&va_arena_token, ID_LVAR, ty_char);
        va_area->type = array_of(ty_char, VA_AREA_SIZE);
        register_ident(va_area);

        func->va_area = va_area;
    }

    // 関数の戻り値の型を保持しておく
    cur_func_type = func_type;

    // 関数のbodyをパース
    expect_token(TK_L_BRACKET);
    func->funcbody = compound_stmt();
    func->stack_size = get_stack_size();
    scope_out();
    add_type(func->funcbody);

    return;
}

static Node* stmt(){
    Token* tok = get_token();
    if(consume_token(TK_RETURN)){
        Node* node = NULL;

        if(cur_func_type->kind == TY_VOID){
            if(consume_token(TK_SEMICORON)){
                node = new_node(ND_RETURN, NULL, NULL);
                node->pos = tok;
                return node;
            } else {
                node = expr();
                node = new_node(ND_RETURN, node, NULL);
                node->pos = tok;
                expect_token(TK_SEMICORON);
                return node;
            }
        } else {
            if(consume_token(TK_SEMICORON)){
                error_tok(tok, "return value is not found.");
            }
            node = expr();
            node = new_node(ND_RETURN, node, NULL);
            node->pos = tok;
            expect_token(TK_SEMICORON);
            return node;
        }
    } else if(consume_token(TK_IF)){
        Node* node = new_node(ND_IF, NULL, NULL);
        node->pos = tok;
        expect_token(TK_L_PAREN);
        node->cond = expr();
        expect_token(TK_R_PAREN);
        node->then = stmt();

        // else側がある場合
        if(consume_token(TK_ELSE)){
            node->elif = stmt();
            node->kind = ND_IF_ELSE;
        }
        return node;
    } else if(consume_token(TK_WHILE)){
        Node* node = new_node(ND_WHILE, NULL, NULL);
        node->pos = tok;
        expect_token(TK_L_PAREN);
        node->cond = expr();
        expect_token(TK_R_PAREN);
        node->body = stmt();
        return node;
    } else if(consume_token(TK_DO)){
        Node* node = new_node(ND_DO_WHILE, NULL, NULL);
        node->pos = tok;
        node->body = stmt();
        expect_token(TK_WHILE);
        expect_token(TK_L_PAREN);
        node->cond = expr();
        expect_token(TK_R_PAREN);
        expect_token(TK_SEMICORON);
        return node;
    } else if(consume_token(TK_FOR)){
        Node* node = new_node(ND_FOR, NULL, NULL);
        node->pos = tok;
        expect_token(TK_L_PAREN);

        // 初期化式
        if(consume_token(TK_SEMICORON)){
            node->init = NULL;
        } else {
            if(is_type()){
                StorageClassKind sck = 0;
                Type* ty = declspec(&sck);
                Ident* ident = declare(ty, sck);
                register_ident(ident);
                if(consume_token(TK_ASSIGN)){
                    Node* var_node = new_node_var(ident);
                    var_node->pos = ident->tok;
                    node->init = new_node(ND_ASSIGN, var_node, assign());
                }
            } else {
                node->init = expr();
            }
            expect_token(TK_SEMICORON);
        }

        // 継続条件の式
        if(consume_token(TK_SEMICORON)){
            node->cond = new_node_num(1);
        } else {
            node->cond = expr();
            expect_token(TK_SEMICORON);
        }

        // イテレートの式
        if(consume_token(TK_R_PAREN)){
            node->incr = NULL;
        } else {
            node->incr = expr();
            expect_token(TK_R_PAREN);
        }
        node->body = stmt();
        return node;
    } else if(consume_token(TK_L_BRACKET)){
        return compound_stmt();
    } else if(consume_token(TK_SEMICORON)){
        Node* node = new_node(ND_VOID_STMT, NULL, NULL);
        node->pos = tok;
        return node;
    } else if(consume_token(TK_BREAK)){
        Node* node = new_node(ND_BREAK, NULL, NULL);
        node->pos = tok;
        return node;
    } else if(consume_token(TK_CONTINUE)){
        Node* node = new_node(ND_CONTINUE, NULL, NULL);
        node->pos = tok;
        return node;
    } else if(consume_token(TK_SWITCH)){
        Node* node = new_node(ND_SWITCH, NULL, NULL);
        Node* switch_back = switch_node;
        switch_node = node;

        node->pos = tok;
        expect_token(TK_L_PAREN);
        node->cond = expr();
        expect_token(TK_R_PAREN);
        node->body = stmt();

        switch_node = switch_back;
        return node;
    } else if(consume_token(TK_CASE)){
        if(switch_node == NULL){
            error_tok(tok, "case is not within a switch statement.");
        }

        Node* node = new_node(ND_CASE, NULL, NULL);
        node->lhs = const_expr(); 
        consume_token(TK_COLON);
        node->pos = tok;

        node->next_case = switch_node->next_case;
        switch_node->next_case = node;
        return node;
    } else if(consume_token(TK_DEFAULT)){
        Node* node = new_node(ND_DEFAULT, NULL, NULL);
        node->pos = tok;
        consume_token(TK_COLON);

        if(switch_node->default_label){
            error_tok(tok, "multiple default label.");
        }
        switch_node->default_label = node;
        return node;
    } else if(is_label()){
        Node* node = new_node(ND_LABEL, NULL, NULL);
        node->pos = tok;
        Token* label_ident = expect_ident();
        expect_token(TK_COLON);

        Label* label = find_label(label_ident);
        if(!label){
            label = register_label(label_ident);
        }
        label->labeld = true;
        node->label = label;
        return  node;
    } else if(consume_token(TK_GOTO)){
        Node* node = new_node(ND_GOTO, NULL, NULL);
        node->pos = tok;
        Token* label_ident = expect_ident();
        Label* label = find_label(label_ident);
        if(!label){
            label = register_label(label_ident);
        }
        node->label = label;
        return  node;
    } else {
        Node* node = expr();
        expect_token(TK_SEMICORON);
        node->pos = tok;
        return node;
    }
}

static Node* compound_stmt(){
    Node* node = new_node(ND_BLOCK, NULL, NULL);

    Node head = {};
    Node* cur = &head;
    while(!consume_token(TK_R_BRACKET)){
        if(is_type()){
            StorageClassKind sck = SCK_NONE;
            Type* ty = declspec(&sck);

            if(consume_token(TK_SEMICORON)){
                continue;
            }

            Node* node = declaration(ty, sck);
            if(node){
                cur = cur->next = node;
            }
        } else {
            cur->next = stmt();
            cur = cur->next;
        }
        add_type(cur);
    }
    node->body = head.next;
    return node;
}

static Node* declaration(Type* ty, StorageClassKind sck){

    if(ty->kind == TY_STRUCT && consume_token(TK_SEMICORON)){
        // 構造体の登録を行う
        return new_node(ND_VOID_STMT, NULL, NULL);
    }

    if(ty->kind == TY_UNION && consume_token(TK_SEMICORON)){
        // 共用体の登録を行う
        return new_node(ND_VOID_STMT, NULL, NULL);
    }

    if(ty->kind == TY_ENUM && consume_token(TK_SEMICORON)){
        // 列挙型の登録を行う
        return new_node(ND_VOID_STMT, NULL, NULL);
    }

    Ident* ident = declare(ty, sck);
    Node* node = NULL;
    if(sck == SCK_TYPEDEF){
        register_typedef(ident, ident->type);
        node = new_node(ND_VOID_STMT, NULL, NULL);
    } else {
        // グローバルスコープならID_GVAR、それ以外はID_LVAR
        ident->kind = (get_current_scope() == get_global_scope()) ? ID_GVAR : ID_LVAR;
        register_ident(ident);

        if(consume_token(TK_ASSIGN)){
            Node* var_node = new_node_var(ident);
            var_node->pos = ident->tok;
            node = new_node(ND_ASSIGN, var_node, assign());
        }
    }

    expect_token(TK_SEMICORON);
    return node;
}

static Ident* declare(Type* ty, StorageClassKind sck){
    while(consume_token(TK_MUL)){
        ty = pointer_to(ty);
    }

    Token* ident_tok = expect_ident();
    if(consume_token(TK_L_SQUARE_BRACKET)){
        int len = expect_num();
        ty = array_of(ty, len);
        expect_token(TK_R_SQUARE_BRACKET);
    }

    Ident* ident = make_ident(ident_tok, ID_LVAR, ty);
    if(sck == SCK_EXTERN){
        ident->is_extern = true;
        ident->kind = ID_GVAR;
    } else if(sck == SCK_STATIC){
        ident->is_static = true;
    }

    return ident;
}


static void count_decl_spec(int* type_flg, int flg, Token* tok){
    // error check
    int target = 0;
    switch(flg){
        case K_LONG:
            // 'long' keyword can use up to 2 times in declaration.
            // ex) long long int
            target = *type_flg & (K_LONG << 1);
            break;
        default:
            target = *type_flg & flg;
            break;
    }

    if(target){
        error_tok(tok, "duplicate type keyword.\n");
    }

    *type_flg += flg;
}

static bool check_storage_class_keyword(StorageClassKind* sck, Token* tok){
    if(consume_token(TK_TYPEDEF)){
        if(*sck != SCK_NONE){
            error_tok(tok, "multiple storage classes in declaration specifies.");
        }
        *sck = SCK_TYPEDEF;
        return true;
    }
    if(consume_token(TK_EXTERN)){
        if(*sck != SCK_NONE){
            error_tok(tok, "multiple storage classes in declaration specifies.");
        }
        *sck = SCK_EXTERN;
        return true;
    }

    if(consume_token(TK_STATIC)){
        if(*sck != SCK_NONE){
            error_tok(tok, "multiple storage classes in declaration specifies.");
        }
        *sck = SCK_STATIC;
        return true;
    }

    // these keyword is recognized but ignored.
    if(consume_token(TK_AUTO)|| consume_token(TK_REGISTER)){
        return true;
    }

    return false;
}

static Type* declspec(StorageClassKind* sck){

    int type_flg = 0;
    bool is_const = false;
    bool is_restrict = false;
    bool is_volatile = false;
    Type* ty = 0;
    while(is_type()){
        Token* tok = get_token();

        // check storage class keyword.
        if(check_storage_class_keyword(sck, tok)){
            continue;
        }

        if(consume_token(TK_STRUCT) || consume_token(TK_UNION)){
            if(ty || type_flg){
                error_tok(tok, "duplicate type keyword.\n");
            }
            ty = struct_or_union_spec(tok->kind == TK_UNION);
            type_flg += K_USER;
            continue;
        }

        if(consume_token(TK_ENUM)){
            if(ty || type_flg){
                error_tok(tok, "duplicate type keyword.\n");
            }
            ty = enum_spec();
            type_flg += K_USER;
            continue;
        }

        if(consume_token(TK_CONST)){
            if(is_const) error_tok(tok, "duplicate type keyword.\n");
            is_const = true;
            continue;
        }

        if(consume_token(TK_RESTRICT)){
            if(is_restrict) error_tok(tok, "duplicate type keyword.\n");
            is_restrict = true;
            continue;
        }

        if(consume_token(TK_VOLATILE)){
            if(is_volatile) error_tok(tok, "duplicate type keyword.\n");
            is_volatile = true;
            continue;
        }

        // check user type
        Token* typedef_name = consume_typedef_name();
        if(typedef_name){
            if(ty || type_flg){
                error_tok(tok, "duplicate type keyword.\n");
            }
            ty = find_typedef(typedef_name)->type;
            type_flg += K_USER;
            continue;
        }

        if(consume_token(TK_BOOL))
            count_decl_spec(&type_flg, K_BOOL, tok);
        if(consume_token(TK_VOID))
            count_decl_spec(&type_flg, K_VOID, tok);
        if(consume_token(TK_CHAR))
            count_decl_spec(&type_flg, K_CHAR, tok);
        if(consume_token(TK_SHORT))
            count_decl_spec(&type_flg, K_SHORT, tok);
        if(consume_token(TK_INT))
            count_decl_spec(&type_flg, K_INT, tok);
        if(consume_token(TK_LONG))
            count_decl_spec(&type_flg, K_LONG, tok);
        if(consume_token(TK_SIGNED))
            count_decl_spec(&type_flg, K_SIGNED, tok);
        if(consume_token(TK_UNSIGNED))
            count_decl_spec(&type_flg, K_UNSIGNED, tok);
    }

    if(!ty){
        switch(type_flg){
            case K_VOID:
                ty = ty_void;
                break;
            case K_BOOL:
                ty = ty_bool;
                break;
            case K_CHAR:
            case K_SIGNED + K_CHAR:
                ty = ty_char;
                break;
            case K_UNSIGNED + K_CHAR:
                ty = ty_uchar;
                break;
            case K_SHORT:
            case K_SHORT + K_INT:
            case K_SIGNED + K_SHORT:
            case K_SIGNED + K_SHORT + K_INT:
                ty = ty_short;
                break;
            case K_UNSIGNED + K_SHORT:
            case K_UNSIGNED + K_SHORT + K_INT:
                ty = ty_ushort;
                break;
            case K_INT:
            case K_SIGNED:
            case K_SIGNED + K_INT:
                ty = ty_int;
                break;
            case K_UNSIGNED:
            case K_UNSIGNED + K_INT:
                ty = ty_uint;
                break;
            case K_LONG:
            case K_LONG + K_INT:
            case K_SIGNED + K_LONG:
            case K_SIGNED + K_LONG + K_INT:
                ty = ty_long;
                break;
            case K_UNSIGNED + K_LONG:
            case K_UNSIGNED + K_LONG + K_INT:
                ty = ty_ulong;
                break;
            default:
                error_tok(get_token(), "Invalid type.\n");
                break;
        }
    }

    if(sck && *sck != SCK_TYPEDEF){
        ty = copy_type(ty);
    }

    if(is_const){
        ty->is_const = true;
    }

    return ty;
}

static Member* struct_or_union_member(){
    Member head;
    Member* cur = &head;
    do {
        cur = cur->next = calloc(1, sizeof(Member));
        StorageClassKind sck = 0;
        Type* ty = declspec(&sck);
        cur->ident = declare(ty, sck);
        expect_token(TK_SEMICORON);
    } while(!consume_token(TK_R_BRACKET));
    return head.next;
}

static Type* struct_or_union_spec(bool is_union){
    /*
        1. 名前付き構造体
        struct ident {
            declspec ident;
            ...
        }

        2. 不完全な名前付き構造体
        struct ident;

        3. 無名構造体
        struct {
            declspec ident;
            ...
        }
    */

    Token* tok = consume_ident();
    if(tok){
        Type* ty = find_tag(tok);

        if(consume_token(TK_L_BRACKET)){
            // 名前付き構造体
            if(ty && !ty->is_imcomplete){
                error_tok(tok, "redefinition of struct.");
            } else if(ty && ty->is_imcomplete){
                // すでに宣言されているが、不完全な構造体
                ty->member = struct_or_union_member();
                ty->is_imcomplete = false;
                
                if(is_union){
                    int max_size = 0;
                    for(Member* cur = ty->member; cur; cur = cur->next){
                        cur->ident->offset = 0;
                        if(max_size < cur->ident->type->size){
                            max_size = cur->ident->type->size;
                        }
                    }
                    ty->size = max_size;
                } else {
                    int offset = 0;
                    for(Member* cur = ty->member; cur; cur = cur->next){
                        cur->ident->offset = offset;
                        offset += cur->ident->type->size;
                    }
                    ty->size = offset;
                }

            } else {
                // まだ登録されていない構造体
                ty = new_type(is_union ? TY_UNION : TY_STRUCT, 0);
                ty->member = struct_or_union_member();
                ty->name = tok;
                ty->is_imcomplete = false;

                if(is_union){
                    int max_size = 0;
                    for(Member* cur = ty->member; cur; cur = cur->next){
                        cur->ident->offset = 0;
                        if(max_size < cur->ident->type->size){
                            max_size = cur->ident->type->size;
                        }
                    }
                    ty->size = max_size;
                } else {
                    int offset = 0;
                    for(Member* cur = ty->member; cur; cur = cur->next){
                        cur->ident->offset = offset;
                        offset += cur->ident->type->size;
                    }
                    ty->size = offset;
                }
                register_tag(ty);
            }
        } else {
            // すでに作っている構造体か、不完全な名前付き構造体
            if(!ty){
                ty = new_type(is_union ? TY_UNION : TY_STRUCT, 0);
                ty->name = tok;
                ty->is_imcomplete = true;
                register_tag(ty);
            }
        }
        return ty;
    } else {
        // 無名構造体
        Type* ty = new_type(is_union ? TY_UNION : TY_STRUCT, 0);
        expect_token(TK_L_BRACKET);
        ty->member = struct_or_union_member();

            if(is_union){
                int max_size = 0;
                for(Member* cur = ty->member; cur; cur = cur->next){
                    cur->ident->offset = 0;
                    if(max_size < cur->ident->type->size){
                        max_size = cur->ident->type->size;
                    }
                }
                ty->size = max_size;
            } else {
                int offset = 0;
                for(Member* cur = ty->member; cur; cur = cur->next){
                    cur->ident->offset = offset;
                    offset += cur->ident->type->size;
                }
                ty->size = offset;
            }
        ty->name = &unnamed_struct_token;
        return ty;
    }
}

static Member* enum_member(){
    // enummember = ident ('=' num)? (',' ident ('=' num)?)* '}'
    Member head;
    Member* cur = &head;
    int val = 0;
    Type* ty = copy_type(ty_int);
    ty->is_const = true;

    cur = cur->next = calloc(1, sizeof(Member));
    Token* tok = expect_ident();
    if(consume_token(TK_ASSIGN)){
        Node* node = cond_expr();
        node = exchange_constant_expr(node);
        val = node->val;
    }
    cur->ident = make_ident(tok, ID_ENUM, ty);
    cur->ident->val = val++;
    register_ident(cur->ident);

    if(!consume_token(TK_R_BRACKET)){
        do {
            expect_token(TK_COMMA);
            Token* tok = consume_ident();
            if(!tok) {
                expect_token(TK_R_BRACKET);
                break;
            }
            cur = cur->next = calloc(1, sizeof(Member));
            if(consume_token(TK_ASSIGN)){
                Node* node = cond_expr();
                node = exchange_constant_expr(node);
                val = node->val;
            }
            cur->ident = make_ident(tok, ID_ENUM, ty);
            cur->ident->val = val++;
            register_ident(cur->ident);
        } while(!consume_token(TK_R_BRACKET));
    }
    return head.next;
}

static Type* enum_spec(){
    Token* tok = consume_ident();
    Type* ty = NULL;

    if(tok){
        ty = find_tag(tok);

        if(consume_token(TK_L_BRACKET)){
            if(ty && !ty->is_imcomplete){
                error_tok(tok, "redefinition of enum.");
            } else if(ty && ty->is_imcomplete){
                ty->member = enum_member();
                ty->is_imcomplete = false;
            } else {
                ty = new_type(TY_ENUM, 4);
                ty->name = tok;
                ty->member = enum_member();
                ty->is_imcomplete = false;
                register_tag(ty);
            }
        } else {
            if(!ty){
                ty = new_type(TY_ENUM, 4);
                ty->name = tok;
                ty->is_imcomplete = true;
                register_tag(ty);
            }
        }
    } else {
        expect_token(TK_L_BRACKET);
        ty = new_type(TY_ENUM, 4);
        ty->name = &unnamed_enum_token;
        ty->member = enum_member();
    }
    return ty;
}

static Node* expr(){
    Node* node = assign();
    while(consume_token(TK_COMMA)){
        node = new_node(ND_COMMA, node, assign());
    }
    return node;
}

static Node* exchange_constant_expr(Node* expr){

    if(expr->kind == ND_NUM){
        return expr;
    }

    expr->lhs = exchange_constant_expr(expr->lhs);
    expr->rhs = exchange_constant_expr(expr->rhs);

    int retval = 0;
    int lhs = expr->lhs->val;
    int rhs = expr->rhs->val;
    switch(expr->kind){
        case ND_ADD:
            retval = lhs + rhs;
            break;
        case ND_SUB:
            retval = lhs - rhs;
            break;
        case ND_MUL:
            retval = lhs * rhs;
            break;
        case ND_DIV:
            retval = lhs / rhs;
            break;
        case ND_MOD:
            retval = lhs % rhs;
            break;
        case ND_EQUAL:
            retval = lhs == rhs;
            break;
        case ND_NOT_EQUAL:
            retval = lhs != rhs;
            break;
        case ND_LT:
            retval = lhs < rhs;
            break;
        case ND_LE:
            retval = lhs <= rhs;
            break;
        case ND_BIT_AND:
            retval = lhs & rhs;
            break;
        case ND_BIT_OR:
            retval = lhs | rhs;
            break;
        case ND_BIT_XOR:
            retval = lhs ^ rhs;
            break;
        case ND_LOGIC_AND:
            retval = lhs && rhs;
            break;
        case ND_LOGIC_OR:
            retval = lhs || rhs;
            break;
        case ND_L_BITSHIFT:
            retval = lhs << rhs;
            break;
        case ND_R_BITSHIFT:
            retval = lhs >> rhs;
            break;
        case ND_COND_EXPR:
        {
            expr->cond = exchange_constant_expr(expr->cond);
            int cond = expr->cond->val;
            retval = cond ? lhs : rhs;
            break;
        }
        default:
            error("It is not constant expression.\n");
    }
    return new_node_num(retval);
}

static Node* assign(){
    Node* node = cond_expr();
    add_type(node);

    Token* tok = get_token();

    if(consume_token(TK_ASSIGN)){
        node = new_node(ND_ASSIGN, node, assign());
    } else if(consume_token(TK_PLUS_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node_add(node, assign()));
    } else if(consume_token(TK_MINUS_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node_sub(node, assign()));
    } else if(consume_token(TK_MUL_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node_mul(node, assign()));
    } else if(consume_token(TK_DIV_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node_div(node, assign()));
    } else if(consume_token(TK_PERCENT_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node_mod(node, assign()));
    } else if(consume_token(TK_L_BITSHIFT_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node(ND_L_BITSHIFT, node, assign()));
    } else if(consume_token(TK_R_BITSHIFT_EQUAL)){
        node = new_node(ND_ASSIGN, node, new_node(ND_R_BITSHIFT, node, assign()));
    }

    if(node->kind == ND_ASSIGN){
        if(node->lhs->type->is_const){
            error_tok(tok, "cannot assign to const.");
        }
    }

    return node;
}

static Node* cond_expr(){
    Node* node = logicOr();
    Token* tok = get_token();
    if(consume_token(TK_QUESTION)){
        Node* cnode = new_node(ND_COND_EXPR, NULL, NULL);
        cnode->cond = node;
        cnode->lhs = expr();
        expect_token(TK_COLON);
        cnode->rhs = cond_expr();
        node = cnode;
        node->pos = tok;
    }
    return node;
}

static Node* logicOr(){
    Node* node = logicAnd();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_PIPE_PIPE)){
            node = new_node(ND_LOGIC_OR, node, logicAnd());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* logicAnd(){
    Node* node = bitOr();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_AND_AND)){
            node = new_node(ND_LOGIC_AND, node, bitOr());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* bitOr(){
    Node* node = bitXor();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_PIPE)){
            node = new_node(ND_BIT_OR, node, bitXor());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* bitXor(){
    Node* node = bitAnd();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_HAT)){
            node = new_node(ND_BIT_XOR, node, bitAnd());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* bitAnd(){
    Node* node = equality();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_AND)){
            node = new_node(ND_BIT_AND, node, equality());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* equality(){
    Node* node = relational();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_EQUAL)){
            node = new_node(ND_EQUAL, node, relational());
            node->pos = tok;
        } else if(consume_token(TK_NOT_EQUAL)){
            node = new_node(ND_NOT_EQUAL, node, relational());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* relational(){
    Node* node = bitShift();
    while(true){
        Token* tok = get_token();
        if(consume_token(TK_L_ANGLE_BRACKET)){
            node = new_node(ND_LT, node, bitShift());
            node->pos = tok;
        } else if(consume_token(TK_L_ANGLE_BRACKET_EQUAL)){
            node = new_node(ND_LE, node, bitShift());
            node->pos = tok;
        } else if(consume_token(TK_R_ANGLE_BRACKET)){
            node = new_node(ND_LT, bitShift(), node);
            node->pos = tok;
        } else if(consume_token(TK_R_ANGLE_BRACKET_EQUAL)){
            node = new_node(ND_LE, bitShift(), node);
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* bitShift(){
    Node* node = add();

    while(true) {
        Token* tok = get_token();
        if(consume_token(TK_L_BITSHIFT)){
            node = new_node(ND_L_BITSHIFT, node, add());
            node->pos = tok;
        } else if(consume_token(TK_R_BITSHIFT)){
            node = new_node(ND_R_BITSHIFT, node, add());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* add(){
    Node* node = mul();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_PLUS)){
            node = new_node(ND_ADD, node, mul());
            node->pos = tok;
        } else if(consume_token(TK_MINUS)){
            node = new_node(ND_SUB, node, mul());
            node->pos = tok;
        } else {
            return node;
        }
    }
}

static Node* mul(){
    Node* node = cast();

    while(true){
        Token* tok = get_token();
        if(consume_token(TK_MUL)){
            node = new_node(ND_MUL, node, cast());
            node->pos = tok;
        } else if(consume_token(TK_DIV)){
            node = new_node(ND_DIV, node, cast());
            node->pos = tok;
        } else if(consume_token(TK_PERCENT)){
            node = new_node(ND_MOD, node, cast());
            node->pos = tok;
        } else {
            break;
        }
    }

    return node;
}

static bool is_cast(){
    bool result = false;
    Token* tok = get_token();

    if(consume_token(TK_L_PAREN)){
        if(is_type()){
            result = true;
        }
    }
    set_token(tok);
    return result;
}

static Node* cast(){
    if(is_cast()){
        Token* tok = get_token();
        expect_token(TK_L_PAREN);
        Type* ty = declspec(NULL);
        while(consume_token(TK_MUL)){
            ty = pointer_to(ty);
        }
        expect_token(TK_R_PAREN);

        Node* node = cast();
        add_type(node);
        node = new_node(ND_CAST, node, NULL);
        node->type = ty;
        node->pos = tok;

        return node;
    }
    return unary();
}

static Node* unary(){
    Token* tok = get_token();
    if(consume_token(TK_PLUS)){
        Node* node = cast();
        node->pos = tok;
        return node;
    } else if(consume_token(TK_MINUS)){
        Node* node = new_node_sub(new_node_num(0), cast());
        node->pos = tok;
        return node;
    } else if(consume_token(TK_AND)){
        Node* node = new_node(ND_ADDR, cast(), NULL);
        node->pos = tok;
        return node;
    } else if(consume_token(TK_MUL)){
        Node* node =  new_node(ND_DREF, cast(), NULL);
        node->pos = tok;
        return node;
    } else if(consume_token(TK_NOT)){
        Node* node =  new_node(ND_NOT, cast(), NULL);
        node->pos = tok;
        return node;
    } else if(consume_token(TK_PLUS_PLUS)){
        Node* node = unary();
        node = new_node(ND_ASSIGN, node, new_node_add(node, new_node_num(1)));
        node->pos = tok;
        return node;
    } else if(consume_token(TK_MINUS_MINUS)){
        Node* node = unary();
        node = new_node(ND_ASSIGN, node, new_node_sub(node, new_node_num(1)));
        node->pos = tok;
        return node;
    } else if(consume_token(TK_SIZEOF)){
        bool is_l_paren = consume_token(TK_L_PAREN);
        Node* node = NULL;
        if(is_type()){
            StorageClassKind sck = 0;
            Type* ty = declspec(&sck);
            while(consume_token(TK_MUL)){
                ty = pointer_to(ty);
            }
            node = new_node_num(ty->size);
        } else {
            node = unary();
            add_type(node);
            if(node->type->kind == TY_ARRAY){
                node = new_node_num(node->type->array_len * node->type->size);
            } else if(node->type->kind == TY_STRUCT || node->type->kind == TY_UNION){
                node = new_node_num(node->type->size);
            } else {
                node = new_node_num(node->type->size);
            }
        }
        if(is_l_paren){
            expect_token(TK_R_PAREN);
        }
        return node;
    }
    return postfix();
}

static Node* postfix(){
    Node* node = primary();

    while(true){
        if(consume_token(TK_L_SQUARE_BRACKET)){
            node = new_node(ND_DREF, new_node_add(node, expr()), NULL);
            expect_token(TK_R_SQUARE_BRACKET);
            continue;
        }

        if(consume_token(TK_PLUS_PLUS)){
            node = new_inc(node);
            continue;
        }

        if(consume_token(TK_MINUS_MINUS)){
            node = new_dec(node);
            continue;
        }

        if(consume_token(TK_DOT)){
            add_type(node);

            node = new_node(ND_MEMBER, node, NULL);

            // find a member
            Token* tok = expect_ident();
            Ident* ident = get_member(node->lhs->type, tok);
            if(!ident){
                error_tok(tok, "Not a member.\n");
            }

            node->type = ident->type;
            node->val = ident->offset;
            continue;
        }

        if(consume_token(TK_ARROW)){
            add_type(node);
            node = new_node(ND_DREF, node, NULL);
            add_type(node);
            node = new_node(ND_MEMBER, node, NULL);

            // find a member
            Token* tok = expect_ident();
            Ident* ident = get_member(node->lhs->type, tok);
            if(!ident){
                error_tok(tok, "Not a member.\n");
            }

            node->type = ident->type;
            node->val = ident->offset;
            continue;
        }

        return node;
    }
}

static Node* primary(){
    Token* pos_tok = get_token();

    if(consume_token(TK_L_PAREN)){
        Node* node = expr();
        expect_token(TK_R_PAREN);
        return node;
    }

    // 文字列リテラル
    Token* tok_str = consume_string_literal();
    if(tok_str){
        Ident* str_ident = register_string_literal(tok_str);
        Node* node = new_node(ND_VAR, 0, 0);
        node->ident = str_ident;
        node->type = str_ident->type;
        return node;
    }

    // ビルトイン関数
    if(consume_token(TK_VA_START)){
        // 暗黙宣言している__va_arena__を取得
        Ident* var_area_ident = find_ident(&va_arena_token);
        Node* var_area_node = new_node_var(var_area_ident);

        expect_token(TK_L_PAREN);
        Node* arg1_node = assign();
        expect_token(TK_COMMA);
        Node* arg2_node = assign();
        expect_token(TK_R_PAREN);

        // 第１引数のポインタの参照を外す
        Node* lhs_node = new_node(ND_DREF, arg1_node, NULL);

        // __builtin_va_elem*型を取得
        Ident* builtin_va_elem_ident = find_typedef(&builtin_va_elem_token);
        Type* ty = builtin_va_elem_ident->type;
        ty = pointer_to(ty);

        // __va_area__を__builtin_va_elem*型にキャストする
        Node* rhs_node = new_node(ND_CAST, var_area_node, NULL);
        rhs_node->type = ty;

        // (__builtin_va_elem*)__va_area__の参照を外す
        rhs_node = new_node(ND_DREF, rhs_node, NULL);

        Node* node = new_node(ND_ASSIGN, lhs_node, rhs_node);
        return node;
    }

    if(consume_token(TK_VA_ARG)){
        expect_token(TK_L_PAREN);
        Node* arg1_node = assign();
        expect_token(TK_COMMA);
        if(!is_type()){
            // ここは型を受け取るべきところ
            error_tok(pos_tok, "expected type keyword or specific type.");
        }
        StorageClassKind sck = 0;
        Type* type = declspec(NULL);
        expect_token(TK_R_PAREN);

        if(is_integer_type(type)){
            // TODO : 引数渡ししか対応していないので、そのようにする

            // gp_offsetを見つける
            arg1_node = new_node(ND_DREF, arg1_node, NULL);
            add_type(arg1_node);
            Node* gp_offset_node = new_node(ND_MEMBER, arg1_node, NULL);
            Ident* gp_offset_ident = get_member(arg1_node->type, &va_elem_gp_offset_token);
            if(!gp_offset_ident){
                error_tok(pos_tok, "[Internal Error] Not found gp_offset.\n");
            }
            gp_offset_node->type = gp_offset_ident->type;
            gp_offset_node->val = gp_offset_ident->offset;

            // reg_save_areaを見つける
            Node* reg_save_area_node = new_node(ND_MEMBER, arg1_node, NULL);
            Ident* reg_save_area_ident = get_member(arg1_node->type, &va_elem_reg_save_area_token);
            if(!reg_save_area_ident){
                error_tok(pos_tok, "[Internal Error] Not found reg_save_area.\n");
            }
            reg_save_area_node->type = reg_save_area_ident->type;
            reg_save_area_node->val = reg_save_area_ident->offset;

            // gp_offset+=8, *(long*)(reg_save_area + (gp_offset - 8))
            Node* gp_offset_inc = new_node(ND_ASSIGN, gp_offset_node, new_node_add(gp_offset_node, new_node_num(8)));

            Node* calc_add = new_node_add(reg_save_area_node, new_node_sub(gp_offset_node, new_node_num(8)));
            Node* cast_adr = new_node(ND_CAST, calc_add, NULL);
            cast_adr->type = pointer_to(ty_long);
            Node* va_arg_node = new_node(ND_DREF, cast_adr, NULL);

            Node* canma_node = new_node(ND_COMMA, gp_offset_inc, va_arg_node);
            return canma_node;
        } else {
            error_tok(pos_tok, "Not implemented va_arg to float type.");
        }
    }

    if(consume_token(TK_VA_END)){
        Node* node = new_node(ND_NOP, 0, 0);
        node->type = ty_void;
        node->pos = pos_tok;
        expect_token(TK_L_PAREN);
        Node* arg1_node = assign();
        expect_token(TK_R_PAREN);
        return node;
    }

    // 変数
    Token* ident_token = consume_ident();
    if(ident_token){
        Ident* ident = find_ident(ident_token);
        if(!ident){
            error_tok(ident_token, "Undefined Variable.");
        } else {
            if((ident->kind == ID_LVAR) || (ident->kind == ID_GVAR)){
                Node* node = new_node(ND_VAR, 0, 0);
                node->ident = ident;
                node->type = ident->type;
                node->pos = ident_token;
                return node;
            } else if(ident->kind == ID_ENUM){
                Node* node = new_node_num(ident->val);
                return node;
            } else if(ident->kind == ID_FUNC){
                if(consume_token(TK_L_PAREN)){
                    Node* node = new_node(ND_FUNCCALL, 0, 0);
                    node->ident = ident;
                    node->type = ident->type;
                    node->pos = ident_token;
                    if(!consume_token(TK_R_PAREN)){
                        Node head = {};
                        Node* nd_param = &head;
                        do{
                            nd_param = nd_param->next = assign();
                        } while(consume_token(TK_COMMA));
                        expect_token(TK_R_PAREN);
                        node->params = head.next;
                    }
                    return node;
                }
                unreachable();
            }
        }
    }

    // 数値リテラル
    return new_node_num(expect_num());
}

static Node* const_expr(){
    Token* tok = get_token();
    Node* node = expr();
    if(node->kind != ND_NUM){
        error_tok(tok, "expected constant expression.");
    }
    return node;
}

static Node* new_node(NodeKind kind, Node* lhs, Node* rhs){
    Node* result = calloc(1, sizeof(Node));
    result->kind = kind;
    result->lhs = lhs;
    result->rhs = rhs;
    return result;
}

static Node* new_node_num(unsigned long num){
    Node* result = calloc(1, sizeof(Node));
    result->val = num;
    return result;
}

static Node* new_node_var(Ident* ident){
    Node* result = calloc(1, sizeof(Node));
    result->kind = ND_VAR;
    result->ident = ident;
    result->type = ident->type;

    return result;
}

static Node* new_node_add(Node* lhs, Node* rhs){

    add_type(lhs);
    add_type(rhs);

    Node* result = new_node(ND_ADD, lhs, rhs);

    // num + num
    if(!lhs->type->ptr_to && !rhs->type->ptr_to)
    {
        return result;
    }
    
    // pointer + pointer
    if(lhs->type->ptr_to && rhs->type->ptr_to){
        error("Try add pointer and pointer.");
    }
    
    // num + pointer
    if(!lhs->type->ptr_to && rhs->type->ptr_to){
        // ポインタ + 数値、になるように入れ替える
        Node* buf = lhs;
        rhs = lhs;
        lhs = buf;
    }

    Node* node_mul = new_node_mul(rhs, new_node_num(lhs->type->ptr_to->size));
    result->lhs = lhs;
    result->rhs = node_mul;

    return result;
}
static Node* new_node_sub(Node* lhs, Node* rhs){

    add_type(lhs);
    add_type(rhs);

    Node* result = new_node(ND_SUB, lhs, rhs);

    // num + num
    if(!lhs->type->ptr_to && !rhs->type->ptr_to)
    {
        return result;
    }
    
    // - pointer
    if(rhs->type->ptr_to){
        error("invalid operand.");
    }
    
    // pointer - num
    if(lhs->type->ptr_to && !rhs->type->ptr_to){
        Node* node_mul = new_node_mul(rhs, new_node_num(lhs->type->ptr_to->size));
        result->lhs = lhs;
        result->rhs = node_mul;
        return result;
    }
}
static Node* new_node_div(Node* lhs, Node* rhs){
    Node* result = new_node(ND_DIV, lhs, rhs);
    return result;
}
static Node* new_node_mul(Node* lhs, Node* rhs){
    Node* result = new_node(ND_MUL, lhs, rhs);
    return result;
}
static Node* new_node_mod(Node* lhs, Node* rhs){
    Node* result = new_node(ND_MOD, lhs, rhs);
    return result;
}

static Node* new_inc(Node* var){
    Token tok;
    tok.pos = "tmp";
    tok.len = 3;
    tok.kind = TK_IDENT;
    scope_in();
    Type* ty = calloc(1, sizeof(Type));
    memcpy(ty, var->type, sizeof(Type));

    Ident* tmp = declare_ident(&tok, ID_LVAR, ty);

    Node* node_tmp = new_node_var(tmp);
    Node* node_assign = new_node(ND_ASSIGN, node_tmp, var);
    Node* node_inc = new_node(ND_ASSIGN, var, new_node_add(var, new_node_num(1)));
    Node* node = new_node(ND_COMMA, node_assign, new_node(ND_COMMA, node_inc, node_tmp));

    scope_out();
    return node;
}

static Node* new_dec(Node* var){
    Token tok;
    tok.pos = "tmp";
    tok.len = 3;
    tok.kind = TK_IDENT;
    scope_in();
    Type* ty = calloc(1, sizeof(Type));
    memcpy(ty, var->type, sizeof(Type));

    Ident* tmp = declare_ident(&tok, ID_LVAR, ty);

    Node* node_tmp = new_node_var(tmp);
    Node* node_assign = new_node(ND_ASSIGN, node_tmp, var);
    Node* node_inc = new_node(ND_ASSIGN, var, new_node_sub(var, new_node_num(1)));
    Node* node = new_node(ND_COMMA, node_assign, new_node(ND_COMMA, node_inc, node_tmp));

    scope_out();
    return node;
}

static bool is_function(){
    Token* bkup = get_token();
    bool retval = false;

    // ポインタの読み取り
    while(consume_token(TK_MUL)){
        ;
    }

    expect_token(TK_IDENT);
    if(consume_token(TK_L_PAREN)){
        retval = true;
    }

    set_token(bkup);
    return retval;
}

// ----------------------------
// トークン操作
void expect_token(TokenKind kind){
    if(token->kind != kind){
        error_tok(token, "error: unexpected token.\n");
    }

    token = token->next;
}

unsigned long expect_num(){
    if(token->kind != TK_NUM){
        error_tok(token, "error: not a number.\n", token->pos);
    }

    unsigned long result = token->val;
    token = token->next;
    return result;
}

bool consume_token(TokenKind kind){
    if(token->kind != kind){
        return false;
    }
    token = token->next;
    return true;
}

Token* consume_ident(){
    if(token->kind != TK_IDENT) return NULL;

    Token* tok = token;
    token = token->next;
    return tok;
}

Token* consume_string_literal(){
    if(token->kind != TK_STRING_LITERAL) return NULL;
    Token* tok = token;
    token = token->next;
    return tok;
}

Token* consume_typedef_name(){
    if(token->kind != TK_IDENT) return NULL;
    Ident* ident = find_typedef(token);
    if(ident == NULL) return NULL;
    Token* typedef_token = token;
    token = token->next;
    return typedef_token;
}

Token* expect_ident(){
    Token* tok = consume_ident();
    if(tok == NULL){
        error_tok(token, "error: not a ident.\n", token->pos);
    }
    return tok;
}

bool is_eof(){
    return token->kind == TK_EOF;
}


bool is_type(){

    // is token registerd as typedef name?
    Ident* ident = find_typedef(token);
    if(ident){
        return true;
    }

    return token->kind == TK_STRUCT
        || token->kind == TK_UNION
        || token->kind == TK_ENUM
        || token->kind == TK_CONST
        || token->kind == TK_VOLATILE
        || token->kind == TK_RESTRICT
        || token->kind == TK_SIGNED
        || token->kind == TK_UNSIGNED
        || token->kind == TK_TYPEDEF
        || token->kind == TK_AUTO
        || token->kind == TK_REGISTER
        || token->kind == TK_RESTRICT
        || token->kind == TK_EXTERN
        || token->kind == TK_STATIC
        || token->kind == TK_BOOL
        || token->kind == TK_VOID
        || token->kind == TK_INT
        || token->kind == TK_LONG
        || token->kind == TK_SHORT
        || token->kind == TK_CHAR;
}

bool is_label(){
    if(token->kind == TK_IDENT && token->next->kind == TK_COLON){
        return true;
    }
    return false;
}

Token* get_token(){
    return token;
}

void set_token(Token* tok){
    token = tok;
}
