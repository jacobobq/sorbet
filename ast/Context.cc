#include "Context.h"
#include "Hashing.h"
#include "common/common.h"

namespace ruby_typer {
namespace ast {

SymbolRef ContextBase::synthesizeClass(UTF8Desc name) {
    auto nameId = enterNameUTF8(name);
    auto symId = getTopLevelClassSymbol(nameId);
    symId.info(*this, true).setCompleted();
    return symId;
}

static const char *init = "initialize";
static UTF8Desc init_DESC{(char *)init, (int)std::strlen(init)};

static const char *andAnd = "&&";
static UTF8Desc andAnd_DESC{(char *)andAnd, (int)std::strlen(andAnd)};

static const char *to_s = "to_s";
static UTF8Desc to_s_DESC{(char *)to_s, (int)std::strlen(to_s)};

static const char *concat = "concat";
static UTF8Desc concat_DESC{(char *)concat, (int)std::strlen(concat)};

static const char *no_symbol_str = "<none>";
static UTF8Desc no_symbol_DESC{(char *)no_symbol_str, (int)std::strlen(no_symbol_str)};

static const char *top_str = "<top>";
static UTF8Desc top_DESC{(char *)top_str, (int)std::strlen(top_str)};

static const char *bottom_str = "<bottom>";
static UTF8Desc bottom_DESC{(char *)bottom_str, (int)std::strlen(bottom_str)};

static const char *root_str = "<root>";
static UTF8Desc root_DESC{(char *)root_str, (int)std::strlen(root_str)};

static const char *nil_str = "nil";
static UTF8Desc nil_DESC{(char *)nil_str, (int)std::strlen(nil_str)};

static const char *todo_str = "<todo sym>";
static UTF8Desc todo_DESC{(char *)todo_str, (int)std::strlen(todo_str)};

ContextBase::ContextBase(spdlog::logger &logger) : logger(logger) {
    unsigned int max_name_count = 262144;   // 6MB
    unsigned int max_symbol_count = 524288; // 32MB

    names.reserve(max_name_count);
    symbols.reserve(max_symbol_count);
    int names_by_hash_size = 2 * max_name_count;
    names_by_hash.resize(names_by_hash_size);
    DEBUG_ONLY(Error::check((names_by_hash_size & (names_by_hash_size - 1)) == 0));

    names.emplace_back(); // first name is used in hashes to indicate empty cell
    // Second name is always <init>, see SymbolInfo::isConstructor
    auto init_id = enterNameUTF8(init_DESC);
    auto andAnd_id = enterNameUTF8(andAnd_DESC);
    auto to_s_id = enterNameUTF8(to_s_DESC);
    auto concat_id = enterNameUTF8(concat_DESC);
    DEBUG_ONLY(Error::check(init_id == Names::initialize()));
    DEBUG_ONLY(Error::check(andAnd_id == Names::andAnd()));
    DEBUG_ONLY(Error::check(to_s_id == Names::to_s()));
    DEBUG_ONLY(Error::check(concat_id == Names::concat()));

    SymbolRef no_symbol_id = synthesizeClass(no_symbol_DESC);
    SymbolRef top_id = synthesizeClass(top_DESC); // BasicObject
    SymbolRef bottom_id = synthesizeClass(bottom_DESC);
    SymbolRef root_id = synthesizeClass(root_DESC);
    SymbolRef nil_id = synthesizeClass(nil_DESC);
    SymbolRef todo_id = synthesizeClass(todo_DESC);

    Error::check(no_symbol_id == noSymbol());
    Error::check(top_id == defn_top());
    Error::check(bottom_id == defn_bottom());
    Error::check(root_id == defn_root());
    Error::check(nil_id == defn_nil());
    Error::check(todo_id == defn_todo());
    /* 0: <none>
     * 1: <top>
     * 2: <bottom>
     * 3: <root>;
     * 4: nil;
     * 5: <todo>
     */
    Error::check(symbols.size() == 6);
}

ContextBase::~ContextBase() {}

constexpr decltype(ContextBase::STRINGS_PAGE_SIZE) ContextBase::STRINGS_PAGE_SIZE;

SymbolRef ContextBase::enterSymbol(SymbolRef owner, NameRef name, SymbolRef result, std::vector<SymbolRef> &args,
                                   bool isMethod) {
    DEBUG_ONLY(Error::check(owner.exists()));
    auto &ownerScope = owner.info(*this, true);
    auto from = ownerScope.members.begin();
    auto to = ownerScope.members.end();
    while (from != to) {
        auto &el = *from;
        if (el.first == name) {
            auto &otherInfo = el.second.info(*this, true);
            if (otherInfo.result() == result && otherInfo.arguments() == args)
                return from->second;
        }
        from++;
    }

    bool reallocate = symbols.size() == symbols.capacity();

    auto ret = SymbolRef(symbols.size());
    symbols.emplace_back();
    auto &info = ret.info(*this, true);
    info.name = name;
    info.flags = 0;

    info.owner = owner;
    info.resultOrParentOrLoader = result;
    if (isMethod)
        info.setMethod();
    else
        info.setField();

    info.argumentsOrMixins.swap(args);
    if (!reallocate)
        ownerScope.members.push_back(std::make_pair(name, ret));
    else
        owner.info(*this, true).members.push_back(std::make_pair(name, ret));
    return ret;
}

NameRef ContextBase::enterNameUTF8(UTF8Desc nm) {
    const auto hs = _hash(nm);
    unsigned int hashTableSize = names_by_hash.size();
    unsigned int mask = hashTableSize - 1;
    auto bucketId = hs & mask;
    unsigned int probe_count = 1;

    while (names_by_hash[bucketId].second) {
        auto &bucket = names_by_hash[bucketId];
        if (bucket.first == hs) {
            auto name_id = bucket.second;
            auto &nm2 = names[name_id];
            if (nm2.kind == NameKind::UTF8 && nm2.raw.utf8 == nm)
                return name_id;
        }
        bucketId = (bucketId + probe_count) & mask;
        probe_count++;
    }
    DEBUG_ONLY(if (probe_count == hashTableSize) { Error::raise("Full table?"); });

    if (names.size() == names.capacity()) {
        expandNames();
        hashTableSize = names_by_hash.size();
        mask = hashTableSize - 1;
        bucketId = hs & mask; // look for place in the new size
        probe_count = 1;
        while (names_by_hash[bucketId].second != 0) {
            bucketId = (bucketId + probe_count) & mask;
            probe_count++;
        }
    }

    auto idx = names.size();
    auto &bucket = names_by_hash[bucketId];
    bucket.first = hs;
    bucket.second = idx;
    names.emplace_back();

    Error::check(nm.to < ContextBase::STRINGS_PAGE_SIZE);

    if (strings_last_page_used + nm.to > ContextBase::STRINGS_PAGE_SIZE) {
        strings.push_back(std::make_unique<std::vector<char>>(ContextBase::STRINGS_PAGE_SIZE));
        // printf("Wasted %i space\n", STRINGS_PAGE_SIZE - strings_last_page_used);
        strings_last_page_used = 0;
    }
    char *from = strings.back()->data() + strings_last_page_used;

    memcpy(from, nm.from, nm.to);
    names[idx].kind = NameKind::UTF8;
    names[idx].raw.utf8.from = from;
    names[idx].raw.utf8.to = nm.to;
    strings_last_page_used += nm.to;

    return idx;
}

void moveNames(std::pair<unsigned int, unsigned int> *from, std::pair<unsigned int, unsigned int> *to,
               unsigned int szFrom, unsigned int szTo) {
    // printf("\nResizing name hash table from %u to %u\n", szFrom, szTo);
    DEBUG_ONLY(Error::check((szTo & (szTo - 1)) == 0));
    DEBUG_ONLY(Error::check((szFrom & (szFrom - 1)) == 0));
    unsigned int mask = szTo - 1;
    for (unsigned int orig = 0; orig < szFrom; orig++) {
        if (from[orig].second) {
            auto hs = from[orig].first;
            unsigned int probe = 1;
            auto bucketId = hs & mask;
            while (to[bucketId].second != 0) {
                bucketId = (bucketId + probe) & mask;
                probe++;
            }
            to[bucketId] = from[orig];
        }
    }
}

void ContextBase::expandNames() {
    Error::check(names.size() == names.capacity());
    Error::check(names.capacity() * 2 == names_by_hash.capacity());
    Error::check(names_by_hash.size() == names_by_hash.capacity());

    names.reserve(names.size() * 2);
    std::vector<std::pair<unsigned int, unsigned int>> new_names_by_hash(names_by_hash.capacity() * 2);
    moveNames(names_by_hash.data(), new_names_by_hash.data(), names_by_hash.capacity(), new_names_by_hash.capacity());
    names_by_hash.swap(new_names_by_hash);
}

NameRef ContextBase::enterNameUnique(UniqueNameKind uniqueNameKind, u2 num, NameRef original) {
    const auto hs = _hash_mix_unique((u2)uniqueNameKind, UNIQUE, num, original.id());
    unsigned int hashTableSize = names_by_hash.size();
    unsigned int mask = hashTableSize - 1;
    auto bucketId = hs & mask;
    unsigned int probe_count = 1;

    while (names_by_hash[bucketId].second != 0 && probe_count < hashTableSize) {
        auto &bucket = names_by_hash[bucketId];
        if (bucket.first == hs) {
            auto &nm2 = names[bucket.second];
            if (nm2.kind == UNIQUE && nm2.unique.uniqueNameKind == uniqueNameKind && nm2.unique.num == num &&
                nm2.unique.original == original)
                return bucket.second;
        }
        bucketId = (bucketId + probe_count) & mask;
        probe_count++;
    }
    if (probe_count == hashTableSize) {
        Error::raise("Full table?");
    }

    if (names.size() == names.capacity()) {
        expandNames();
        hashTableSize = names_by_hash.size();
        mask = hashTableSize - 1;

        bucketId = hs & mask; // look for place in the new size
        probe_count = 1;
        while (names_by_hash[bucketId].second != 0) {
            bucketId = (bucketId + probe_count) & mask;
            probe_count++;
        }
    }

    auto &bucket = names_by_hash[bucketId];
    bucket.first = hs;
    bucket.second = names.size();

    auto idx = names.size();
    names.emplace_back();

    names[idx].kind = UNIQUE;
    names[idx].unique.num = num;
    names[idx].unique.uniqueNameKind = uniqueNameKind;
    names[idx].unique.original = original;
    return idx;
}

SymbolRef ContextBase::getTopLevelClassSymbol(NameRef name) {
    auto &current = classes[name];
    if (current.exists()) {
        return current; // return fast
    }
    current = symbols.size();
    symbols.emplace_back();
    auto &info = current.info(*this, true); // allowing noSymbol is needed because this enters noSymbol.
    info.name = name;
    info.owner = defn_root();
    info.flags = 0;
    info.setClass();
    return current;
}

SymbolRef ContextBase::newTemporary(UniqueNameKind kind, NameRef name, SymbolRef owner) {
    auto tempName = this->enterNameUnique(kind, freshNameId++, name);
    std::vector<SymbolRef> empty;
    return this->enterSymbol(owner, tempName, SymbolRef(), empty, false);
}

} // namespace ast
} // namespace ruby_typer