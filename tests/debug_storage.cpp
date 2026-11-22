#include <iostream>
#include "runtime/include/platform.h"
#include "runtime/include/lexer.h"
#include "runtime/include/axon_parser.h"

int main() {
    std::string src =
        "flow F { input = \"hello\" }\n"
        "storage DB { type = \"sqlite\" }\n"
        "agent A { provider ollama\nmodel llama3\n"
        "main { return \"done\" } }\n"
        "graph { A -> done [on_success] }\n";

    axon::Lexer lex(src, "<test>");
    auto tokens = lex.tokenize();
    std::cout << "Tokens: " << tokens.size() << "\n";
    for (auto& t : tokens)
        std::cout << "  [" << (int)t.type << "] "
                  << t.type_name() << " = '" << t.value << "'\n";
    return 0;
}
