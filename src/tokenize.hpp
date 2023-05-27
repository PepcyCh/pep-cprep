#pragma once

#include "utils.hpp"

namespace pep::cprep {

enum class TokenType {
    eEof,
    eIdentifier,
    eString,
    eChar,
    eNumber,
    eSharp,
    eDoubleSharp,
    eDot,
    eTripleDots,
    eLeftBracketRound,
    eRightBracketRound,
    eLeftBracketSquare,
    eRightBracketSquare,
    eLeftBracketCurly,
    eRightBracketCurly,
    eAdd,
    eSub,
    eMul,
    eDiv,
    eMod,
    eInc,
    eDec,
    eBAnd,
    eBOr,
    eBXor,
    eBNot,
    eBShl,
    eBShr,
    eLAnd,
    eLOr,
    eLNot,
    eAddEq,
    eSubEq,
    eMulEq,
    eDivEq,
    eModEq,
    eBAndEq,
    eBOrEq,
    eBXorEq,
    eBShlEq,
    eBShrEq,
    eLess,
    eLessEq,
    eGreater,
    eGreaterEq,
    eEq,
    eNotEq,
    eSpaceship,
    eAssign,
    eArrow,
    eQuestion,
    eColon,
    eSemicolon,
    eComma,
    eScope,
    eUnknown,
};

struct Token final {
    TokenType type;
    std::string_view value;
};

Token get_next_token(InputState &input, std::string &output, bool space_cross_line = true);

}
