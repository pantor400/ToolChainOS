#include "cpp_runner.h"
#include "console.h"

#define MAX_TOKENS 1024
#define MAX_VARS 32
#define MAX_FUNCS 16
#define MAX_PARAMS 8
#define MAX_FRAMES 16
#define MAX_STEPS 500000

typedef enum {
    TOK_EOF = 0,
    TOK_INT_KW,
    TOK_VOID_KW,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_COUT,
    TOK_PRINT,
    TOK_PRINTLN,
    TOK_IDENT,
    TOK_NUM,
    TOK_STR,
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACE,     /* { */
    TOK_RBRACE,     /* } */
    TOK_SEMI,       /* ; */
    TOK_COMMA,      /* , */
    TOK_ASSIGN,     /* = */
    TOK_PLUS_ASSIGN,  /* += */
    TOK_MINUS_ASSIGN, /* -= */
    TOK_STAR_ASSIGN,  /* *= */
    TOK_SLASH_ASSIGN, /* /= */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_SLASH,      /* / */
    TOK_PERCENT,    /* % */
    TOK_INC,        /* ++ */
    TOK_DEC,        /* -- */
    TOK_EQ,         /* == */
    TOK_NEQ,        /* != */
    TOK_LT,         /* < */
    TOK_LE,         /* <= */
    TOK_GT,         /* > */
    TOK_GE,         /* >= */
    TOK_AND,        /* && */
    TOK_OR,         /* || */
    TOK_NOT,        /* ! */
    TOK_SHL         /* << */
} tok_type_t;

typedef struct {
    tok_type_t type;
    int line;
    int64_t num;
    char text[48];
} token_t;

typedef struct {
    char name[32];
    int64_t val;
} var_t;

typedef struct {
    var_t vars[MAX_VARS];
    int var_count;
} frame_t;

typedef struct {
    char name[32];
    int param_count;
    char params[MAX_PARAMS][32];
    int body_token;
} func_t;

typedef enum {
    EXEC_OK = 0,
    EXEC_RETURN,
    EXEC_BREAK,
    EXEC_CONTINUE,
    EXEC_ERROR
} exec_status_t;

static token_t tokens[MAX_TOKENS];
static int token_count = 0;
static int cur_tok = 0;

static func_t funcs[MAX_FUNCS];
static int func_count = 0;

static frame_t frames[MAX_FRAMES];
static int frame_count = 0;

static int64_t last_return_val = 0;
static exec_status_t exec_status = EXEC_OK;
static uint32_t step_counter = 0;
static char error_msg[96];
static int error_line = 0;

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < max_len) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static void set_error(int line, const char *msg) {
    exec_status = EXEC_ERROR;
    error_line = line;
    str_copy(error_msg, msg, sizeof(error_msg));
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');
}

static int is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

/* --- Lexer --- */
static int tokenize(const char *src) {
    token_count = 0;
    int line = 1;
    const char *p = src;

    while (*p && token_count + 1 < MAX_TOKENS) {
        /* Espacios en blanco */
        if (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
            continue;
        }
        if (*p == '\n') {
            line++;
            p++;
            continue;
        }
        /* Comentarios // */
        if (*p == '/' && *(p + 1) == '/') {
            p += 2;
            while (*p && *p != '\n') p++;
            continue;
        }
        /* Comentarios multilínea */
        if (*p == '/' && *(p + 1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p + 1) == '/')) {
                if (*p == '\n') line++;
                p++;
            }
            if (*p) p += 2;
            continue;
        }

        token_t *t = &tokens[token_count];
        t->line = line;
        t->num = 0;
        t->text[0] = 0;

        /* Cadenas entre comillas "..." */
        if (*p == '"') {
            p++;
            size_t len = 0;
            while (*p && *p != '"' && len + 1 < sizeof(t->text)) {
                if (*p == '\\') {
                    p++;
                    if (*p == 'n') t->text[len++] = '\n';
                    else if (*p == 't') t->text[len++] = '\t';
                    else if (*p == '\\') t->text[len++] = '\\';
                    else if (*p == '"') t->text[len++] = '"';
                    else if (*p) t->text[len++] = *p;
                } else {
                    t->text[len++] = *p;
                }
                if (*p) p++;
            }
            t->text[len] = 0;
            if (*p == '"') p++;
            t->type = TOK_STR;
            token_count++;
            continue;
        }

        /* Numeros */
        if (is_digit(*p)) {
            int64_t val = 0;
            while (is_digit(*p)) {
                val = val * 10 + (*p - '0');
                p++;
            }
            t->type = TOK_NUM;
            t->num = val;
            token_count++;
            continue;
        }

        /* Identificadores y palabras clave */
        if (is_alpha(*p)) {
            size_t len = 0;
            while (is_alnum(*p) && len + 1 < sizeof(t->text)) {
                t->text[len++] = *p++;
            }
            t->text[len] = 0;

            if (str_eq(t->text, "int")) t->type = TOK_INT_KW;
            else if (str_eq(t->text, "void")) t->type = TOK_VOID_KW;
            else if (str_eq(t->text, "return")) t->type = TOK_RETURN;
            else if (str_eq(t->text, "if")) t->type = TOK_IF;
            else if (str_eq(t->text, "else")) t->type = TOK_ELSE;
            else if (str_eq(t->text, "while")) t->type = TOK_WHILE;
            else if (str_eq(t->text, "for")) t->type = TOK_FOR;
            else if (str_eq(t->text, "break")) t->type = TOK_BREAK;
            else if (str_eq(t->text, "continue")) t->type = TOK_CONTINUE;
            else if (str_eq(t->text, "cout")) t->type = TOK_COUT;
            else if (str_eq(t->text, "print")) t->type = TOK_PRINT;
            else if (str_eq(t->text, "println")) t->type = TOK_PRINTLN;
            else t->type = TOK_IDENT;

            token_count++;
            continue;
        }

        /* Operadores y simbolos */
        if (*p == '+' && *(p + 1) == '+') { t->type = TOK_INC; p += 2; }
        else if (*p == '-' && *(p + 1) == '-') { t->type = TOK_DEC; p += 2; }
        else if (*p == '+' && *(p + 1) == '=') { t->type = TOK_PLUS_ASSIGN; p += 2; }
        else if (*p == '-' && *(p + 1) == '=') { t->type = TOK_MINUS_ASSIGN; p += 2; }
        else if (*p == '*' && *(p + 1) == '=') { t->type = TOK_STAR_ASSIGN; p += 2; }
        else if (*p == '/' && *(p + 1) == '=') { t->type = TOK_SLASH_ASSIGN; p += 2; }
        else if (*p == '=' && *(p + 1) == '=') { t->type = TOK_EQ; p += 2; }
        else if (*p == '!' && *(p + 1) == '=') { t->type = TOK_NEQ; p += 2; }
        else if (*p == '<' && *(p + 1) == '=') { t->type = TOK_LE; p += 2; }
        else if (*p == '>' && *(p + 1) == '=') { t->type = TOK_GE; p += 2; }
        else if (*p == '&' && *(p + 1) == '&') { t->type = TOK_AND; p += 2; }
        else if (*p == '|' && *(p + 1) == '|') { t->type = TOK_OR; p += 2; }
        else if (*p == '<' && *(p + 1) == '<') { t->type = TOK_SHL; p += 2; }
        else if (*p == '=') { t->type = TOK_ASSIGN; p++; }
        else if (*p == '+') { t->type = TOK_PLUS; p++; }
        else if (*p == '-') { t->type = TOK_MINUS; p++; }
        else if (*p == '*') { t->type = TOK_STAR; p++; }
        else if (*p == '/') { t->type = TOK_SLASH; p++; }
        else if (*p == '%') { t->type = TOK_PERCENT; p++; }
        else if (*p == '<') { t->type = TOK_LT; p++; }
        else if (*p == '>') { t->type = TOK_GT; p++; }
        else if (*p == '!') { t->type = TOK_NOT; p++; }
        else if (*p == '(') { t->type = TOK_LPAREN; p++; }
        else if (*p == ')') { t->type = TOK_RPAREN; p++; }
        else if (*p == '{') { t->type = TOK_LBRACE; p++; }
        else if (*p == '}') { t->type = TOK_RBRACE; p++; }
        else if (*p == ';') { t->type = TOK_SEMI; p++; }
        else if (*p == ',') { t->type = TOK_COMMA; p++; }
        else {
            /* Caracter desconocido ignorado */
            p++;
            continue;
        }
        token_count++;
    }

    tokens[token_count].type = TOK_EOF;
    tokens[token_count].line = line;
    tokens[token_count].text[0] = 0;
    tokens[token_count].num = 0;
    return 1;
}

/* --- Gestion de variables y entorno --- */
static int find_var(const char *name, int64_t *out_val) {
    for (int f = frame_count - 1; f >= 0; f--) {
        for (int i = 0; i < frames[f].var_count; i++) {
            if (str_eq(frames[f].vars[i].name, name)) {
                if (out_val) *out_val = frames[f].vars[i].val;
                return 1;
            }
        }
    }
    return 0;
}

static int set_var(const char *name, int64_t val) {
    for (int f = frame_count - 1; f >= 0; f--) {
        for (int i = 0; i < frames[f].var_count; i++) {
            if (str_eq(frames[f].vars[i].name, name)) {
                frames[f].vars[i].val = val;
                return 1;
            }
        }
    }
    return 0;
}

static int declare_var(const char *name, int64_t val, int line) {
    if (frame_count <= 0) return 0;
    frame_t *f = &frames[frame_count - 1];
    for (int i = 0; i < f->var_count; i++) {
        if (str_eq(f->vars[i].name, name)) {
            set_error(line, "VARIABLE REDECLARADA");
            return 0;
        }
    }
    if (f->var_count >= MAX_VARS) {
        set_error(line, "DEMASIADAS VARIABLES LOCALES");
        return 0;
    }
    str_copy(f->vars[f->var_count].name, name, sizeof(f->vars[f->var_count].name));
    f->vars[f->var_count].val = val;
    f->var_count++;
    return 1;
}

/* Declaraciones de expresiones */
static int64_t eval_expr(void);
static void exec_stmt(void);
static void exec_block(void);

static int match(tok_type_t type) {
    if (tokens[cur_tok].type == type) {
        cur_tok++;
        return 1;
    }
    return 0;
}

static int peek(tok_type_t type) {
    return tokens[cur_tok].type == type;
}

/* Evaluacion de expresiones */
static int64_t eval_primary(void) {
    if (peek(TOK_NUM)) {
        int64_t v = tokens[cur_tok].num;
        cur_tok++;
        return v;
    }
    if (peek(TOK_IDENT)) {
        char name[32];
        str_copy(name, tokens[cur_tok].text, sizeof(name));
        int tok_line = tokens[cur_tok].line;
        cur_tok++;

        /* Llamada a funcion */
        if (match(TOK_LPAREN)) {
            int64_t args[MAX_PARAMS];
            int arg_c = 0;
            if (!match(TOK_RPAREN)) {
                do {
                    if (arg_c < MAX_PARAMS) {
                        args[arg_c++] = eval_expr();
                    } else {
                        eval_expr();
                    }
                } while (match(TOK_COMMA));
                match(TOK_RPAREN);
            }
            /* Buscar funcion */
            int f_idx = -1;
            for (int i = 0; i < func_count; i++) {
                if (str_eq(funcs[i].name, name)) {
                    f_idx = i;
                    break;
                }
            }
            if (f_idx < 0) {
                set_error(tok_line, "FUNCION NO DEFINIDA");
                return 0;
            }
            /* Llamar funcion */
            if (frame_count >= MAX_FRAMES) {
                set_error(tok_line, "DESBORDAMIENTO DE PILA DE LLAMADAS");
                return 0;
            }
            frames[frame_count].var_count = 0;
            for (int i = 0; i < arg_c && i < funcs[f_idx].param_count; i++) {
                str_copy(frames[frame_count].vars[i].name, funcs[f_idx].params[i], 32);
                frames[frame_count].vars[i].val = args[i];
                frames[frame_count].var_count++;
            }
            frame_count++;
            int saved_tok = cur_tok;
            cur_tok = funcs[f_idx].body_token;
            exec_block();
            int64_t ret = last_return_val;
            if (exec_status == EXEC_RETURN) exec_status = EXEC_OK;
            cur_tok = saved_tok;
            frame_count--;
            return ret;
        }

        /* Post-incremento o post-decremento */
        int64_t val = 0;
        if (!find_var(name, &val)) {
            set_error(tok_line, "VARIABLE NO DECLARADA");
            return 0;
        }
        if (match(TOK_INC)) {
            set_var(name, val + 1);
            return val;
        }
        if (match(TOK_DEC)) {
            set_var(name, val - 1);
            return val;
        }
        return val;
    }
    if (match(TOK_LPAREN)) {
        int64_t val = eval_expr();
        match(TOK_RPAREN);
        return val;
    }
    set_error(tokens[cur_tok].line, "EXPRESION NO VALIDA");
    return 0;
}

static int64_t eval_unary(void) {
    if (match(TOK_PLUS)) return eval_unary();
    if (match(TOK_MINUS)) return -eval_unary();
    if (match(TOK_NOT)) return !eval_unary();
    if (match(TOK_INC)) {
        if (peek(TOK_IDENT)) {
            char name[32];
            str_copy(name, tokens[cur_tok].text, sizeof(name));
            cur_tok++;
            int64_t val = 0;
            find_var(name, &val);
            val++;
            set_var(name, val);
            return val;
        }
    }
    if (match(TOK_DEC)) {
        if (peek(TOK_IDENT)) {
            char name[32];
            str_copy(name, tokens[cur_tok].text, sizeof(name));
            cur_tok++;
            int64_t val = 0;
            find_var(name, &val);
            val--;
            set_var(name, val);
            return val;
        }
    }
    return eval_primary();
}

static int64_t eval_multiplicative(void) {
    int64_t left = eval_unary();
    while (peek(TOK_STAR) || peek(TOK_SLASH) || peek(TOK_PERCENT)) {
        tok_type_t op = tokens[cur_tok].type;
        int op_line = tokens[cur_tok].line;
        cur_tok++;
        int64_t right = eval_unary();
        if (op == TOK_STAR) {
            left = left * right;
        } else if (op == TOK_SLASH) {
            if (right == 0) {
                set_error(op_line, "DIVISION POR CERO");
                return 0;
            }
            left = left / right;
        } else if (op == TOK_PERCENT) {
            if (right == 0) {
                set_error(op_line, "RESTO CON MODULO CERO");
                return 0;
            }
            left = left % right;
        }
    }
    return left;
}

static int64_t eval_additive(void) {
    int64_t left = eval_multiplicative();
    while (peek(TOK_PLUS) || peek(TOK_MINUS)) {
        tok_type_t op = tokens[cur_tok].type;
        cur_tok++;
        int64_t right = eval_multiplicative();
        if (op == TOK_PLUS) left = left + right;
        else left = left - right;
    }
    return left;
}

static int64_t eval_relational(void) {
    int64_t left = eval_additive();
    while (peek(TOK_LT) || peek(TOK_LE) || peek(TOK_GT) || peek(TOK_GE)) {
        tok_type_t op = tokens[cur_tok].type;
        cur_tok++;
        int64_t right = eval_additive();
        if (op == TOK_LT) left = (left < right);
        else if (op == TOK_LE) left = (left <= right);
        else if (op == TOK_GT) left = (left > right);
        else if (op == TOK_GE) left = (left >= right);
    }
    return left;
}

static int64_t eval_equality(void) {
    int64_t left = eval_relational();
    while (peek(TOK_EQ) || peek(TOK_NEQ)) {
        tok_type_t op = tokens[cur_tok].type;
        cur_tok++;
        int64_t right = eval_relational();
        if (op == TOK_EQ) left = (left == right);
        else left = (left != right);
    }
    return left;
}

static int64_t eval_logical_and(void) {
    int64_t left = eval_equality();
    while (peek(TOK_AND)) {
        cur_tok++;
        int64_t right = eval_equality();
        left = (left && right);
    }
    return left;
}

static int64_t eval_logical_or(void) {
    int64_t left = eval_logical_and();
    while (peek(TOK_OR)) {
        cur_tok++;
        int64_t right = eval_logical_and();
        left = (left || right);
    }
    return left;
}

static int64_t eval_assignment(void) {
    if (peek(TOK_IDENT)) {
        int next_idx = cur_tok + 1;
        tok_type_t next_type = tokens[next_idx].type;
        if (next_type == TOK_ASSIGN || next_type == TOK_PLUS_ASSIGN ||
            next_type == TOK_MINUS_ASSIGN || next_type == TOK_STAR_ASSIGN ||
            next_type == TOK_SLASH_ASSIGN) {
            char name[32];
            str_copy(name, tokens[cur_tok].text, sizeof(name));
            int tok_line = tokens[cur_tok].line;
            cur_tok += 2;
            int64_t right = eval_assignment();
            int64_t cur_val = 0;
            if (!find_var(name, &cur_val)) {
                set_error(tok_line, "VARIABLE NO DECLARADA");
                return 0;
            }
            if (next_type == TOK_ASSIGN) cur_val = right;
            else if (next_type == TOK_PLUS_ASSIGN) cur_val += right;
            else if (next_type == TOK_MINUS_ASSIGN) cur_val -= right;
            else if (next_type == TOK_STAR_ASSIGN) cur_val *= right;
            else if (next_type == TOK_SLASH_ASSIGN) {
                if (right == 0) {
                    set_error(tok_line, "DIVISION POR CERO");
                    return 0;
                }
                cur_val /= right;
            }
            set_var(name, cur_val);
            return cur_val;
        }
    }
    return eval_logical_or();
}

static int64_t eval_expr(void) {
    return eval_assignment();
}

/* Saltador de sentencias / bloques cuando se evaluan if/else o bucles */
static void skip_stmt(void) {
    if (match(TOK_LBRACE)) {
        int depth = 1;
        while (depth > 0 && !peek(TOK_EOF)) {
            if (match(TOK_LBRACE)) depth++;
            else if (match(TOK_RBRACE)) depth--;
            else cur_tok++;
        }
        return;
    }
    while (!peek(TOK_SEMI) && !peek(TOK_EOF)) {
        cur_tok++;
    }
    match(TOK_SEMI);
}

/* --- Sentencias --- */
static void exec_block(void) {
    if (!match(TOK_LBRACE)) {
        exec_stmt();
        return;
    }
    while (!peek(TOK_RBRACE) && !peek(TOK_EOF) && exec_status == EXEC_OK) {
        exec_stmt();
    }
    match(TOK_RBRACE);
}

static void exec_stmt(void) {
    if (exec_status != EXEC_OK || peek(TOK_EOF)) return;

    step_counter++;
    if (step_counter > MAX_STEPS) {
        set_error(tokens[cur_tok].line, "LIMITE DE PASOS EXCEDIDO (BUCLE INFINITO)");
        return;
    }

    /* Bloque anidado */
    if (peek(TOK_LBRACE)) {
        exec_block();
        return;
    }

    /* Punto y coma vacio */
    if (match(TOK_SEMI)) return;

    /* Declaracion de variable: int x = 5, y = 10; */
    if (match(TOK_INT_KW)) {
        do {
            if (!peek(TOK_IDENT)) {
                set_error(tokens[cur_tok].line, "SE ESPERABA NOMBRE DE VARIABLE");
                return;
            }
            char name[32];
            str_copy(name, tokens[cur_tok].text, sizeof(name));
            int tok_line = tokens[cur_tok].line;
            cur_tok++;

            int64_t val = 0;
            if (match(TOK_ASSIGN)) {
                val = eval_expr();
            }
            if (exec_status != EXEC_OK) return;
            declare_var(name, val, tok_line);
        } while (match(TOK_COMMA));
        match(TOK_SEMI);
        return;
    }

    /* Return */
    if (match(TOK_RETURN)) {
        if (!peek(TOK_SEMI)) {
            last_return_val = eval_expr();
        } else {
            last_return_val = 0;
        }
        match(TOK_SEMI);
        exec_status = EXEC_RETURN;
        return;
    }

    /* If */
    if (match(TOK_IF)) {
        match(TOK_LPAREN);
        int64_t cond = eval_expr();
        match(TOK_RPAREN);
        if (exec_status != EXEC_OK) return;

        if (cond) {
            exec_stmt();
            if (match(TOK_ELSE)) {
                skip_stmt();
            }
        } else {
            skip_stmt();
            if (match(TOK_ELSE)) {
                exec_stmt();
            }
        }
        return;
    }

    /* While */
    if (match(TOK_WHILE)) {
        int cond_tok = cur_tok;
        match(TOK_LPAREN);
        int64_t cond = eval_expr();
        match(TOK_RPAREN);

        int body_tok = cur_tok;
        while (cond && exec_status == EXEC_OK) {
            exec_stmt();
            if (exec_status == EXEC_BREAK) {
                exec_status = EXEC_OK;
                break;
            }
            if (exec_status == EXEC_CONTINUE) {
                exec_status = EXEC_OK;
            }
            if (exec_status != EXEC_OK) break;
            cur_tok = cond_tok;
            match(TOK_LPAREN);
            cond = eval_expr();
            match(TOK_RPAREN);
            cur_tok = body_tok;
        }
        if (!cond && exec_status == EXEC_OK) {
            cur_tok = body_tok;
            skip_stmt();
        }
        return;
    }

    /* For (init; cond; step) body */
    if (match(TOK_FOR)) {
        match(TOK_LPAREN);
        /* Init */
        if (!peek(TOK_SEMI)) {
            if (match(TOK_INT_KW)) {
                char name[32];
                str_copy(name, tokens[cur_tok].text, sizeof(name));
                int line = tokens[cur_tok].line;
                cur_tok++;
                int64_t val = 0;
                if (match(TOK_ASSIGN)) val = eval_expr();
                declare_var(name, val, line);
            } else {
                eval_expr();
            }
        }
        match(TOK_SEMI);

        int cond_tok = cur_tok;
        int64_t cond = 1;
        if (!peek(TOK_SEMI)) cond = eval_expr();
        match(TOK_SEMI);

        int step_tok = cur_tok;
        while (!peek(TOK_RPAREN) && !peek(TOK_EOF)) cur_tok++;
        match(TOK_RPAREN);

        int body_tok = cur_tok;
        while (cond && exec_status == EXEC_OK) {
            exec_stmt();
            if (exec_status == EXEC_BREAK) {
                exec_status = EXEC_OK;
                break;
            }
            if (exec_status == EXEC_CONTINUE) {
                exec_status = EXEC_OK;
            }
            if (exec_status != EXEC_OK) break;
            /* Step */
            cur_tok = step_tok;
            if (!peek(TOK_RPAREN)) eval_expr();
            /* Reeval cond */
            cur_tok = cond_tok;
            cond = 1;
            if (!peek(TOK_SEMI)) cond = eval_expr();
            cur_tok = body_tok;
        }
        if (!cond && exec_status == EXEC_OK) {
            cur_tok = body_tok;
            skip_stmt();
        }
        return;
    }

    /* Break / Continue */
    if (match(TOK_BREAK)) {
        match(TOK_SEMI);
        exec_status = EXEC_BREAK;
        return;
    }
    if (match(TOK_CONTINUE)) {
        match(TOK_SEMI);
        exec_status = EXEC_CONTINUE;
        return;
    }

    /* cout << ... */
    if (match(TOK_COUT)) {
        while (match(TOK_SHL)) {
            if (peek(TOK_STR)) {
                console_write(tokens[cur_tok].text);
                cur_tok++;
            } else {
                int64_t val = eval_expr();
                console_signed(val);
            }
        }
        match(TOK_SEMI);
        return;
    }

    /* print(...) y println(...) */
    if (match(TOK_PRINT) || match(TOK_PRINTLN)) {
        int is_ln = (tokens[cur_tok - 1].type == TOK_PRINTLN);
        match(TOK_LPAREN);
        if (peek(TOK_STR)) {
            console_write(tokens[cur_tok].text);
            cur_tok++;
        } else {
            int64_t val = eval_expr();
            console_signed(val);
        }
        match(TOK_RPAREN);
        match(TOK_SEMI);
        if (is_ln) console_putc('\n');
        return;
    }

    /* Expresion generica */
    eval_expr();
    match(TOK_SEMI);
}

/* --- Parseo inicial de funciones y punto de entrada --- */
static int collect_functions(void) {
    func_count = 0;
    cur_tok = 0;
    while (!peek(TOK_EOF)) {
        /* Buscar int nombre(...) o void nombre(...) */
        if (peek(TOK_INT_KW) || peek(TOK_VOID_KW)) {
            cur_tok++;
            if (peek(TOK_IDENT)) {
                char fname[32];
                str_copy(fname, tokens[cur_tok].text, sizeof(fname));
                cur_tok++;

                if (match(TOK_LPAREN)) {
                    func_t *fn = &funcs[func_count];
                    str_copy(fn->name, fname, sizeof(fn->name));
                    fn->param_count = 0;

                    if (!match(TOK_RPAREN)) {
                        do {
                            if (match(TOK_INT_KW) || match(TOK_VOID_KW)) {
                                if (peek(TOK_IDENT)) {
                                    str_copy(fn->params[fn->param_count++], tokens[cur_tok].text, 32);
                                    cur_tok++;
                                }
                            }
                        } while (match(TOK_COMMA) && fn->param_count < MAX_PARAMS);
                        match(TOK_RPAREN);
                    }

                    if (peek(TOK_LBRACE)) {
                        fn->body_token = cur_tok;
                        func_count++;
                        skip_stmt();
                        continue;
                    }
                }
            }
        }
        cur_tok++;
    }
    return 1;
}

int cpp_run_script(const char *source, int64_t *return_val) {
    if (!source || !*source) {
        console_write("EL SCRIPT C++ ESTA VACIO.\n");
        return 0;
    }

    step_counter = 0;
    exec_status = EXEC_OK;
    last_return_val = 0;
    error_msg[0] = 0;
    error_line = 0;
    frame_count = 0;

    if (!tokenize(source)) {
        console_write("ERROR AL PROCESAR LOS CARACTERES DEL CODIGO.\n");
        return 0;
    }

    collect_functions();

    /* Buscar main() */
    int main_idx = -1;
    for (int i = 0; i < func_count; i++) {
        if (str_eq(funcs[i].name, "main")) {
            main_idx = i;
            break;
        }
    }

    /* Marco inicial */
    frame_count = 1;
    frames[0].var_count = 0;

    if (main_idx >= 0) {
        cur_tok = funcs[main_idx].body_token;
        exec_block();
    } else {
        /* No se encontro main, intentar ejecutar desde el inicio */
        cur_tok = 0;
        while (!peek(TOK_EOF) && exec_status == EXEC_OK) {
            exec_stmt();
        }
    }

    if (exec_status == EXEC_ERROR) {
        console_write("ERROR C++ EN LINEA ");
        console_dec(error_line);
        console_write(": ");
        console_write(error_msg);
        console_putc('\n');
        return 0;
    }

    if (return_val) {
        *return_val = last_return_val;
    }
    return 1;
}
