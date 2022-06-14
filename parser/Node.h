#ifndef SORBET_PARSER_NODE_H
#define SORBET_PARSER_NODE_H

#include "common/common.h"
#include "core/core.h"
#include <memory>
#include <string>

namespace sorbet::parser {

#include "parser/Node_gen_tag.h"

struct NodeDeleter;

class Node {
    friend NodeDeleter;
    void deleteTagged();

public:
    Node(NodeTag tag, core::LocOffsets loc) : tag(tag), loc(loc) {
        ENFORCE(loc.exists(), "Location of parser node is none");
    }
    virtual ~Node() = default;
    virtual std::string toStringWithTabs(const core::GlobalState &gs, int tabs = 0) const = 0;
    std::string toString(const core::GlobalState &gs) const {
        return toStringWithTabs(gs);
    }
    virtual std::string toJSON(const core::GlobalState &gs, int tabs = 0) = 0;
    virtual std::string toJSONWithLocs(const core::GlobalState &gs, core::FileRef file, int tabs = 0) = 0;
    virtual std::string toWhitequark(const core::GlobalState &gs, int tabs = 0) = 0;
    virtual std::string nodeName() = 0;
    NodeTag tag;
    core::LocOffsets loc;

protected:
    void printTabs(fmt::memory_buffer &to, int count) const;
    void printNode(fmt::memory_buffer &to, const std::unique_ptr<Node, NodeDeleter> &node, const core::GlobalState &gs,
                   int tabs) const;
    void printNodeJSON(fmt::memory_buffer &to, const std::unique_ptr<Node, NodeDeleter> &node, const core::GlobalState &gs,
                       int tabs) const;
    void printNodeJSONWithLocs(fmt::memory_buffer &to, const std::unique_ptr<Node, NodeDeleter> &node, const core::GlobalState &gs,
                               core::FileRef file, int tabs) const;
    void printNodeWhitequark(fmt::memory_buffer &to, const std::unique_ptr<Node, NodeDeleter> &node, const core::GlobalState &gs,
                             int tabs) const;
};

struct NodeDeleter {
    void operator()(Node *node) {
        node->deleteTagged();
    }
};

template <class To> To *cast_node(Node *what) {
    static_assert(!std::is_pointer<To>::value, "To must not be a pointer");
    static_assert(std::is_assignable<Node *&, To *>::value, "Ill Formed To, has to be a subclass of Node");
    static_assert(std::is_final<To>::value, "To is not final");
    return fast_cast<Node, To>(what);
}

template <class To> bool isa_node(Node *what) {
    return cast_node<To>(what) != nullptr;
}

template <class N, typename... Args>
std::unique_ptr<Node, NodeDeleter> make_node(Args &&... args) {
    return std::unique_ptr<Node, NodeDeleter>(new N(std::forward<Args>(args)...));
}

using NodeVec = InlinedVector<std::unique_ptr<Node, NodeDeleter>, 4>;

#include "parser/Node_gen.h"
}; // namespace sorbet::parser

#endif
