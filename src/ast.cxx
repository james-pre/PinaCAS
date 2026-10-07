#include "ast.hxx"

#include <cstdlib>
#include <utility>

ast *ast::make(::num *value) {
	ast *node = new ast(Type::Number);
	node->num_ = value;
	return node;
}

ast *ast::make(Sym symbol) {
	ast *node = new ast(Type::Symbol);
	node->symbol_ = symbol;
	return node;
}

ast *ast::make(Op op) {
	ast *node = new ast(Type::Operator);
	node->op_ = {op, nullptr};
	return node;
}

ast *ast::make(Op op, ast *operand) {
	ast *node = make(op);
	node->appendChild(operand);
	return node;
}

ast *ast::make(Op op, ast *left, ast *right) {
	ast *node = make(op);
	node->appendChild(left);
	node->appendChild(right);
	return node;
}

void ast::dispose(ast *node) {
	delete node;
}

ast::ast(const ast &other) : type_(other.type_) {
	switch (type_) {
		case Type::Number: num_ = other.num_->copy(); break;
		case Type::Symbol: symbol_ = other.symbol_; break;
		case Type::Operator: {
			op_ = {other.op_.type, nullptr};

			ast **link = &op_.content;
			for (const ast &child : other.children()) {
				*link = new ast(child);
				link = &(*link)->next_;
			}
			break;
		}
	}
}

ast::ast(ast &&other) noexcept : type_(Type::Symbol), symbol_(Sym::Invalid) {
	takeContents(other);
}

ast &ast::operator=(const ast &other) {
	if (this != &other)
		*this = ast(other);
	return *this;
}

ast &ast::operator=(ast &&other) noexcept {
	if (this != &other) {
		const ast previous(std::move(*this));
		takeContents(other);
	}
	return *this;
}

ast::~ast() {
	switch (type_) {
		case Type::Number: ::num::dispose(num_); break;
		case Type::Symbol: break;
		case Type::Operator: {
			ast *child = op_.content;
			while (child != nullptr) {
				ast *next = child->next_;
				delete child;
				child = next;
			}
			break;
		}
	}
}

void ast::replace(ast *replacement) {
	removeChild(*replacement);
	*this = std::move(*replacement);
	delete replacement;
}

void ast::takeContents(ast &other) {
	type_ = other.type_;

	switch (type_) {
		case Type::Number: num_ = other.num_; break;
		case Type::Symbol: symbol_ = other.symbol_; break;
		case Type::Operator: op_ = other.op_; break;
	}

	other.type_ = Type::Symbol;
	other.symbol_ = Sym::Invalid;
}

ast *ast::copy() const {
	return new ast(*this);
}

bool ast::compare(const ast &other) const {
	if (this == &other)
		return true;

	if (type_ != other.type_)
		return false;

	switch (type_) {
		case Type::Number: return *num_ == *other.num_;
		case Type::Symbol: return symbol_ == other.symbol_;
		case Type::Operator: {
			if (op_.type != other.op_.type)
				return false;

			const size_t length = childCount();
			if (length != other.childCount())
				return false;

			if (!is_op_commutative(op_.type)) {
				const ast *b = other.op_.content;
				for (const ast &a : children()) {
					if (!a.compare(*b))
						return false;
					b = b->next_;
				}

				return true;
			}

			bool *used = new bool[length]();
			bool matched = true;

			for (const ast &a : children()) {
				matched = false;
				size_t index = 0;

				for (const ast &b : other.children()) {
					if (!used[index] && a.compare(b)) {
						used[index] = true;
						matched = true;
						break;
					}
					index++;
				}

				if (!matched)
					break;
			}

			delete[] used;

			return matched;
		}
	}

	return false;
}

bool ast::operator==(const ast &other) const {
	return compare(other);
}

ast *ast::lastChild() {
	return const_cast<ast *>(std::as_const(*this).lastChild());
}

const ast *ast::lastChild() const {
	const ast *current = firstChild();
	if (current == nullptr)
		return nullptr;

	while (current->next_ != nullptr)
		current = current->next_;
	return current;
}

ast *ast::childAt(size_t index) {
	return const_cast<ast *>(std::as_const(*this).childAt(index));
}

const ast *ast::childAt(size_t index) const {
	const ast *current = firstChild();
	while (current != nullptr && index-- > 0)
		current = current->next_;
	return current;
}

size_t ast::childCount() const {
	size_t count = 0;
	for (const ast *child = firstChild(); child != nullptr; child = child->next_)
		count++;
	return count;
}

size_t ast::indexOf(const ast &child) const {
	size_t index = 0;
	for (const ast &current : children()) {
		if (&current == &child)
			return index;
		index++;
	}
	return npos;
}

Error ast::appendChild(ast *child) {
	if (!isOperator())
		return Error::AstNotAllowed;

	ast *last = lastChild();
	if (last == nullptr)
		op_.content = child;
	else
		last->next_ = child;
	child->next_ = nullptr;

	return Error::Success;
}

Error ast::insertChild(ast *child, size_t index) {
	if (!isOperator())
		return Error::AstNotAllowed;

	ast **link = &op_.content;
	while (index-- > 0) {
		if (*link == nullptr)
			return Error::AstOutOfBounds;
		link = &(*link)->next_;
	}

	child->next_ = *link;
	*link = child;

	return Error::Success;
}

void ast::takeChildren(ast &other) {
	if (!isOperator() || !other.isOperator() || other.op_.content == nullptr)
		return;

	ast *moved = other.op_.content;
	other.op_.content = nullptr;

	ast *last = lastChild();
	if (last == nullptr)
		op_.content = moved;
	else
		last->next_ = moved;
}

ast *ast::unlink(ast **link) {
	ast *removed = *link;
	if (removed != nullptr) {
		*link = removed->next_;
		removed->next_ = nullptr;
	}
	return removed;
}

ast *ast::removeChild(const ast &child) {
	if (!isOperator())
		return nullptr;

	ast **link = &op_.content;
	while (*link != nullptr && *link != &child)
		link = &(*link)->next_;
	return unlink(link);
}

ast *ast::removeChildAt(size_t index) {
	if (!isOperator())
		return nullptr;

	ast **link = &op_.content;
	while (*link != nullptr && index-- > 0)
		link = &(*link)->next_;
	return unlink(link);
}
