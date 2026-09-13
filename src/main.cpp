#include <peglib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>

// ============================================================
// ВСТРОЕННАЯ ГРАММАТИКА C++ (базовая семантика)
// ============================================================
// Точка расширения: DslTerm — сюда подставляются внешние DSL-термины.
// NullLiteral уже здесь — это часть C++.
// ============================================================

const char* CPP_GRAMMAR = R"que(
  Program     <- _ Statement* _

  Statement   <- FunctionDecl / VarDecl / IfStmt / ReturnStmt / ExprStmt

  FunctionDecl <- Type _ Ident _ "(" _ Params? _ ")" _ Block
  Params      <- Param ("," _ Param)*
  Param       <- Type _ Ident

  VarDecl     <- Type _ Ident _ "=" _ Expr _ ";" _
  IfStmt      <- "if" _ "(" _ Expr _ ")" _ Block
  ReturnStmt  <- "return" _ Expr? _ ";" _
  ExprStmt    <- Expr _ ";" _
  Block       <- "{" _ Statement* _ "}" _

  Type        <- BaseType _ "*"*
  BaseType    <- "int" / "void" / "bool" / "auto" / Ident

  Expr        <- AssignExpr
  AssignExpr  <- Comparison (_ "=" _ AssignExpr)?
  Comparison  <- Primary (_ "==" _ Primary)?
  Primary     <- DslTerm / Call / NullLiteral / Ident / NumLiteral / "(" _ Expr _ ")" / "&" _ Ident

  Call        <- Ident _ "(" _ Args? _ ")"
  Args        <- Expr ("," _ Expr)*

  NullLiteral <- "NULL"
  NumLiteral  <- < [0-9]+ >
  Ident       <- < [a-zA-Z_][a-zA-Z0-9_]* >

  _           <- [ \t\r\n]*
)que";

    // ============================================================
    // ЗАГРУЗКА И ОБЪЕДИНЕНИЕ ГРАММАТИК
    // ============================================================
    // Внешняя грамматика подставляется в точку расширения DslTerm.
    // ============================================================

    std::string build_combined_grammar(const std::string & external_dsl) {
    // Извлекаем правила из внешней грамматики
    // Формат: Name <- "keyword" _ "(" ... ")"
    // Для простоты — просто вставляем всё содержимое в DslTerm.

    std::string combined = CPP_GRAMMAR;

    // Находим точку расширения и заменяем DslTerm на внешние правила
    // В нашем случае: DslTerm <- DslBFS
    // DslBFS описано во внешней грамматике.

    // Заменяем "DslTerm" в CPP_GRAMMAR на конкретные правила из внешней
    size_t pos = combined.find("Primary     <- DslTerm");
    if (pos != std::string::npos) {
        // Подставляем внешние DSL-термины вместо DslTerm
        // Предполагаем, что внешняя грамматика содержит правило BFS
        combined.replace(pos, std::string("Primary     <- DslTerm").size(),
                         "Primary     <- BFS / Call / NullLiteral / Ident / NumLiteral");
    }

    // Добавляем внешние правила в конец
    combined += "\n\n" + external_dsl;

    return combined;
}

// ============================================================
// ВНУТРЕННЕЕ ПРЕДСТАВЛЕНИЕ ПРАВИЛА
// ============================================================

struct Rule {
    std::string name;
    std::string phase;
    int priority = 0;
    std::string match_node;
    std::string action;        // "replace" или "generate"
    std::string action_text;
    std::string risk;
};

// ============================================================
// ГРАММАТИКА ФОРМАТА rules.meta (зашита в движок)
// ============================================================

const char *RULES_GRAMMAR = R"(
  Rules       <- _ Rule* _
  Rule        <- "rule" _ Ident _ "{" _ RuleBody _ "}"
  RuleBody    <- (Phase / Priority / Match / Replace / Generate / Risk)*
  Phase       <- "phase" _ ":" _ Ident _ ";"? _
  Priority    <- "priority" _ ":" _ Number _ ";"? _
  Match       <- "match" _ ":" _ MatchBody _
  MatchBody   <- "node" _ ":" _ Ident _ ";"? _
  Replace     <- "replace" _ ":" _ ReplaceBody _
  ReplaceBody <- < [^\n]+ > _
  Generate    <- "generate" _ ":" _ GenerateBody _
  GenerateBody<- "<<<" < (!">>>" .)* > ">>>" _
  Risk        <- "risk" _ ":" _ Ident _ ";"? _
  Ident       <- < [a-zA-Z_][a-zA-Z0-9_]* >
  Number      <- < [0-9]+ >
  _           <- [ \t\r\n]*
)";

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================

std::string read_file(const std::string &path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

// ============================================================
// ПАРСИНГ rules.meta
// ============================================================

std::vector<Rule> parse_rules(const std::string &rules_text) {
    peg::parser parser;
    /*parser.set_logger([](size_t line, size_t col, const std::string &msg) {
        std::cerr << "Ошибка парсинга на " << line << ":" << col << " - " << msg << "\n";
    });*/
    parser.set_logger(static_cast<peg::Log>([](size_t line, size_t col, const std::string &msg, const std::string &rule) {
        std::cerr << "[" << rule << "] " << line << ":" << col << " - " << msg << "\n";
    }));
    parser.load_grammar(RULES_GRAMMAR);
    parser.enable_ast();

    std::shared_ptr<peg::Ast> ast;
    if (!parser.parse(rules_text.c_str(), ast)) {
        throw std::runtime_error("Failed to parse rules.meta");
    }

    std::vector<Rule> rules;
    for (auto &node : ast->nodes) {
        if (node->name != "Rule") continue;

        Rule r;
        for (auto &child : node->nodes) {
            if (child->name == "Ident" && r.name.empty()) r.name = child->token;
            if (child->name == "Phase")    r.phase = child->token;
            if (child->name == "Priority") r.priority = std::stoi(std::string(child->token));
            if (child->name == "MatchBody") {
                for (auto &m : child->nodes) {
                    if (m->name == "Ident") r.match_node = m->token;
                }
            }
            if (child->name == "ReplaceBody") {
                r.action = "replace";
                r.action_text = child->token;
            }
            if (child->name == "GenerateBody") {
                r.action = "generate";
                r.action_text = child->token;
            }
            if (child->name == "Risk") r.risk = child->token;
        }
        rules.push_back(r);
    }

    // Сортируем по приоритету (детерминированный порядок)
    std::sort(rules.begin(), rules.end(),
              [](const Rule &a, const Rule &b) {
        return a.priority < b.priority;
    });

    return rules;
}

// ============================================================
// ОБХОД AST С ПРИМЕНЕНИЕМ ПРАВИЛ
// ============================================================

// Извлекает значение узла по имени (для подстановок в generate)
std::string find_child_token(const std::shared_ptr<peg::Ast> &node,
                             const std::string &name) {
    for (auto &child : node->nodes) {
        if (child->name == name) return std::string(child->token);
    }
    return "";
}

// Подставляет ${name} в шаблоне значениями из узлов
std::string expand_template(const std::string &tmpl,
                            const std::shared_ptr<peg::Ast> &node) {
    std::string result = tmpl;
    size_t pos = 0;
    while ((pos = result.find("${", pos)) != std::string::npos) {
        size_t end = result.find("}", pos);
        if (end == std::string::npos) break;
        std::string var = result.substr(pos + 2, end - pos - 2);
        std::string value = find_child_token(node, var);
        result.replace(pos, end - pos + 1, value);
        pos += value.size();
    }
    return result;
}

void apply_rules(const std::shared_ptr<peg::Ast> &node,
                 const std::vector<Rule> &rules,
                 std::string &output) {
    // Проверяем, матчится ли узел под правило
    for (const auto &rule : rules) {
        if (node->name == rule.match_node) {
            if (rule.action == "replace") {
                output += rule.action_text;
            }
            else if (rule.action == "generate") {
                output += expand_template(rule.action_text, node);
            }
            return;
        }
    }

    // Терминальный узел — выводим токен
    if (node->nodes.empty()) {
        output += node->token;
        return;
    }

    // Нетерминальный — обходим детей
    for (auto &child : node->nodes) {
        apply_rules(child, rules, output);
    }
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0]
            << " <dsl.peg> <rules.meta> <input.cpp>\n";
        return 1;
    }

    try {
        std::string dsl_grammar = read_file(argv[1]);
        std::string rules_text = read_file(argv[2]);
        std::string input_text = read_file(argv[3]);

        // 1. Объединяем встроенную C++ грамматику с внешней DSL
        std::string combined = build_combined_grammar(dsl_grammar);
        // 2. Парсим входной код
        peg::parser parser;
        parser.set_logger([](size_t line, size_t col, const std::string &msg) {
            std::cerr << "Ошибка парсинга на " << line << ":" << col << " - " << msg << "\n";
        });
        parser.load_grammar(combined);
        if (!parser) {
            std::cerr << "Failed dsl\n";
            return 1;
        }
        parser.enable_ast();

        std::shared_ptr<peg::Ast> ast;
        if (!parser.parse(input_text.c_str(), ast)) {
            std::cerr << "Failed to parse input.cpp\n";
            return 1;
        }

        // 3. Парсим правила
        auto rules = parse_rules(rules_text);
        std::cerr << "Loaded " << rules.size() << " rule(s)\n";

        // 4. Применяем правила
        std::string output;
        apply_rules(ast, rules, output);

        // 5. Выводим результат
        std::cout << output << "\n";

    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}