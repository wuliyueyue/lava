#ifndef __LAVA_HXX__
#define __LAVA_HXX__

#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <memory>
#include <tuple>
#include <sstream>
#include <algorithm>
#include <iterator>

// This garbage makes the ORM map integer-vectors to INTEGER[] type in Postgres
// instead of making separate tables. Important for uniqueness constraints to
// work!
#pragma db map type("INTEGER\\[\\]") as("TEXT") to("(?)::INTEGER[]") from("(?)::TEXT")
typedef std::vector<uint32_t> uint32_t_vec;
#pragma db value(uint32_t_vec) type("INTEGER[]")

#pragma db object
struct SourceLval { // was DuaKey
#pragma db id auto
    unsigned long id;

    std::string file;
    uint32_t line;
    std::string ast_name;   // AST node definition.

    // When did we see taint?
    enum Timing {
        NULL_TIMING = 0,
        BEFORE_OCCURRENCE = 1,
        AFTER_OCCURRENCE = 2
    } timing;

#pragma db index("SourceLvalUniq") unique members(file, line, ast_name, timing)

    bool operator<(const SourceLval &other) const {
        return std::tie(file, line, ast_name, timing) <
            std::tie(other.file, other.line, other.ast_name, other.timing);
    }

    friend std::ostream &operator<<(std::ostream &os, const SourceLval &m) {
        os << "Lval [" << m.file << ":" << m.line << " ";
        os << "\"" << m.ast_name << "\"]";
        return os;
    }
};

#pragma db object
struct LabelSet {
#pragma db id auto
    unsigned long id;

    uint64_t ptr;           // Pointer to labelset during taint run
    std::string inputfile;  // Inputfile used for this run.

    std::vector<uint32_t> labels;

#pragma db index("LabelSetUniq") unique members(ptr, inputfile, labels)

    bool operator<(const LabelSet &other) const {
        return std::tie(ptr, inputfile, labels) <
            std::tie(other.ptr, other.inputfile, other.labels);
    }
};

#pragma db object
struct Dua {
#pragma db id auto
    unsigned long id;

#pragma db not_null
    const SourceLval* lval;

    // Labelset for each byte, in sequence, at shoveling point.
    std::vector<const LabelSet*> viable_bytes;

    std::vector<uint32_t> all_labels;

    // Inputfile used when this dua appeared.
    std::string inputfile;

    // max tcn of any byte of this lval
    uint32_t max_tcn;
    // max cardinality of taint set for lval
    uint32_t max_cardinality;

    uint64_t instr;     // instr count
    bool fake_dua;      // true iff this dua is fake (corresponds to untainted bytes)

#pragma db index("DuaUniq") unique members(lval, inputfile, instr)

    bool operator<(const Dua &other) const {
         return std::tie(lval, viable_bytes, inputfile, max_tcn,
                         max_cardinality, instr, fake_dua) <
             std::tie(other.lval, other.viable_bytes, other.inputfile,
                     other.max_tcn, other.max_cardinality, other.instr,
                     other.fake_dua);
    }

    operator std::string() const {
        std::stringstream os;
        os << *this;
        return os.str();
    }

    friend std::ostream &operator<<(std::ostream &os, const Dua &dua) {
        os << "DUA [" << dua.inputfile << "][" << *dua.lval << ",";
        os << "[{";
        auto it = std::ostream_iterator<uint64_t>(os, "}, {");
        for (const LabelSet *ls : dua.viable_bytes) {
            *it++ = ls ? ls->ptr : 0;
        }
        os << "}]," << "{";
        std::copy(dua.all_labels.begin(), dua.all_labels.end(),
                std::ostream_iterator<uint32_t>(os, ","));
        os << "}," << dua.max_tcn;
        os << "," << dua.max_cardinality << "," << dua.instr;
        os << "," << (dua.fake_dua ? "fake" : "real");
        os << "]";
        return os;
    }

};

#pragma db object
struct AttackPoint {
#pragma db id auto
    unsigned long id;

    std::string file;
    uint32_t line;

    enum Type {
        ATP_FUNCTION_CALL,
        ATP_POINTER_RW,
        ATP_LARGE_BUFFER_AVAIL
    } type;

#pragma db index("AttackPointUniq") unique members(file, line, type)

    bool operator<(const AttackPoint &other) const {
        return std::tie(file, line, type) <
            std::tie(other.file, other.line, other.type);
    }

    operator std::string() const {
        std::stringstream os;
        os << *this;
        return os.str();
    }

    friend std::ostream &operator<<(std::ostream &os, const AttackPoint &m) {
        os << "ATP [" << m.file << ":" << m.line << "] {";
        if (m.type == ATP_POINTER_RW) {
            os << "ATP_POINTER_RW";
        } else if (m.type == ATP_FUNCTION_CALL) {
            os << "ATP_FUNCTION_CALL";
        } else assert(false);
        os << "}";
        return os;
    }
};

#pragma db object
struct Bug {
#pragma db id auto
    unsigned long id;

#pragma db not_null
    const Dua* dua;
    std::vector<uint32_t> selected_bytes;

#pragma db not_null
    const AttackPoint* atp;

    float max_liveness;

#pragma db index("BugUniq") unique members(atp, dua, selected_bytes)

    bool operator<(const Bug &other) const {
         return std::tie(atp, dua, selected_bytes) <
             std::tie(other.atp, other.dua, other.selected_bytes);
    }
};

// Corresponds to one (Lval, )
#pragma db object
struct SourceModification {
#pragma db id auto
    unsigned long id = 0;

#pragma db not_null
    const SourceLval* lval = nullptr;
    std::vector<uint32_t> selected_bytes;
    uint64_t selected_bytes_hash = 0;

#pragma db not_null
    const AttackPoint* atp = nullptr;

#pragma db index("SourceModificationUniq") unique members(atp, lval, selected_bytes)

    SourceModification() {}
    SourceModification(const SourceLval *_lval, std::vector<uint32_t> _bytes,
            const AttackPoint* _atp)
        : lval(_lval), selected_bytes(_bytes), atp(_atp) {
        selected_bytes_hash = 0;
        for (size_t i = 0; i < selected_bytes.size(); i++) {
            selected_bytes_hash ^= (selected_bytes[i] + 1) << (16 * (i % 4));
        }
    }

    bool operator<(const SourceModification &other) const {
         return std::tie(atp, lval, selected_bytes_hash) <
             std::tie(other.atp, other.lval, other.selected_bytes_hash);
    }
};

#pragma db object
struct Build {
#pragma db id auto
    unsigned long id;

#pragma db value_not_null
    // Bugs that were inserted into this build
    std::vector<const Bug*> bugs;

    std::string output;    // path to executable
    bool compile;           // did the build compile?

    bool operator<(const Build &other) const {
        return std::tie(bugs, output, compile) <
            std::tie(other.bugs, other.output, other.compile);
    }
};

#pragma db object
struct Run {
#pragma db id auto
    unsigned long id;

#pragma db not_null
    const Build* build;
    const Bug* fuzzed;      // was this run on fuzzed or orig input?
    int exitcode;           // exit code of program
    std::string output;     // output of program
    bool success;           // true unless python script failed somehow.

    bool operator<(const Run &other) const {
        return std::tie(build, fuzzed, exitcode, output, success) <
            std::tie(other.build, other.fuzzed, other.exitcode,
                    other.output, other.success);
    }
};

#pragma db object
struct SourceFunction {
#pragma db id auto
    unsigned long id;

    std::string file;       // Definition filename
    uint32_t line;          // Definition line
    std::string name;       // Function name

#pragma db index("SourceFunctionUniq") unique members(file, line, name)

    bool operator<(const SourceFunction &other) const {
        return std::tie(file, line, name) <
            std::tie(other.file, other.line, other.name);
    }
};

#pragma db object
struct Call {
#pragma db id auto
    unsigned long id;

    uint64_t call_instr;    // Instruction count at call
    uint64_t ret_instr;     // Instruction count at ret

#pragma db not_null
    const SourceFunction* called_function;
    std::string callsite_file;
    uint32_t callsite_line;

#pragma db index("CallUniq") unique members(call_instr, ret_instr, called_function, callsite_file, callsite_line)

    bool operator<(const Call &other) const {
        return std::tie(call_instr, ret_instr, called_function,
                callsite_file, callsite_line) <
            std::tie(other.call_instr, other.ret_instr,
                    other.called_function, other.callsite_file,
                    other.callsite_line);
    }
};
#endif
