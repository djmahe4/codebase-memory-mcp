#include "tree_sitter/parser.h"

enum TokenType {
    BLOCK_COMMENT,
    LINE_COMMENT,
    SLASH,
    NEWLINE,
};

void *tree_sitter_wren_external_scanner_create(void) {
    return NULL;
}

void tree_sitter_wren_external_scanner_destroy(void *payload) {}

unsigned tree_sitter_wren_external_scanner_serialize(void *payload, char *buffer) {
    return 0;
}

void tree_sitter_wren_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {}

static void advance(TSLexer *lexer) {
    lexer->advance(lexer, false);
}

static void skip(TSLexer *lexer) {
    lexer->advance(lexer, true);
}

bool tree_sitter_wren_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    // Skip spaces and tabs (but not newlines - those are significant)
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
        skip(lexer);
    }

    // If NEWLINE is valid and we see a newline, consume it
    if (valid_symbols[NEWLINE] && lexer->lookahead == '\n') {
        advance(lexer);
        lexer->mark_end(lexer);

        // Skip any additional whitespace and newlines (treating consecutive newlines as one)
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            advance(lexer);
        }

        lexer->result_symbol = NEWLINE;
        return true;
    }

    if (!valid_symbols[BLOCK_COMMENT] && !valid_symbols[LINE_COMMENT] && !valid_symbols[SLASH]) {
        return false;
    }

    // If we're looking for comments/slash, skip newlines too (comments are extras)
    // But only if NEWLINE is not valid (otherwise we should have returned NEWLINE above)
    if (!valid_symbols[NEWLINE]) {
        while (lexer->lookahead == '\n') {
            skip(lexer);
            // Skip any trailing whitespace after newline
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
                skip(lexer);
            }
        }
    }

    // Check for tokens starting with /
    if (lexer->lookahead != '/') {
        return false;
    }

    // We see /. Consume it and peek at the next character.
    advance(lexer);

    // Mark the end after consuming /. This will be the token end for SLASH.
    lexer->mark_end(lexer);

    // Block comment /*
    if (lexer->lookahead == '*') {
        if (!valid_symbols[BLOCK_COMMENT]) {
            // Block comment not valid here, but we've consumed /.
            // Check if SLASH is valid
            if (valid_symbols[SLASH]) {
                lexer->result_symbol = SLASH;
                return true;
            }
            return false;
        }

        advance(lexer);  // consume *

        int depth = 1;

        while (depth > 0 && !lexer->eof(lexer)) {
            if (lexer->lookahead == '/') {
                advance(lexer);
                if (lexer->lookahead == '*') {
                    advance(lexer);
                    depth++;
                }
            } else if (lexer->lookahead == '*') {
                advance(lexer);
                if (lexer->lookahead == '/') {
                    advance(lexer);
                    depth--;
                }
            } else {
                advance(lexer);
            }
        }

        if (depth == 0) {
            lexer->mark_end(lexer);
            lexer->result_symbol = BLOCK_COMMENT;
            return true;
        }
        // Unclosed block comment - return false
        return false;
    }

    // Line comment //
    if (lexer->lookahead == '/') {
        if (!valid_symbols[LINE_COMMENT]) {
            if (valid_symbols[SLASH]) {
                lexer->result_symbol = SLASH;
                return true;
            }
            return false;
        }

        advance(lexer);  // consume second /

        while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && !lexer->eof(lexer)) {
            advance(lexer);
        }

        lexer->mark_end(lexer);
        lexer->result_symbol = LINE_COMMENT;
        return true;
    }

    // Just a single slash (division operator)
    if (valid_symbols[SLASH]) {
        lexer->result_symbol = SLASH;
        return true;
    }

    return false;
}
