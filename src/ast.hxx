#pragma once

#include "imath.hxx"
#include "error.hxx"

#include <cstddef>
#include <cstdlib>

enum class Op : unsigned char {
	/*nary*/
	Add,
	Mult,
	Div,

	Pow,
	Root,
	Log,

	/*1st child = f(var), 2nd child = var, 3rd child = value for var*/
	Deriv,
	/*1st child = f(var), 2nd child = var*/
	Integral,

	/*1st child = left side, 2nd child = right side*/
	Equals,
	/*Derivative of the unknown function that is its child*/
	Prime,
	/*1st child = function, 2nd child = point it is evaluated at*/
	At,
	/*Shown in work only: 1st child = term, 2nd child = index, 3rd child = lower limit of a sum to infinity*/
	Sum,
	/*Shown in work only: 1st child = name, 2nd child = subscript*/
	Subscript,

	/*Unary*/
	Factorial,

	Int,
	Abs,

	Sin,
	Sin_Inv,
	Cos,
	Cos_Inv,
	Tan,
	Tan_Inv,

	SinH,
	SinH_Inv,
	CosH,
	CosH_Inv,
	TanH,
	TanH_Inv,

	Amount
};

constexpr bool is_op_commutative(Op op) {
	return op == Op::Add || op == Op::Mult;
}

constexpr bool is_op_operator(Op op) {
	return op >= Op::Add && op <= Op::Log;
}

constexpr bool is_op_function(Op op) {
	return op >= Op::Int && op <= Op::TanH_Inv;
}

constexpr bool is_op_nary(Op op) {
	return op >= Op::Add && op <= Op::Log;
}

constexpr bool is_op_unary(Op op) {
	return op >= Op::Factorial && op <= Op::TanH_Inv;
}

enum class Sym : unsigned char {
	A = 'A',
	B,
	C,
	D,
	E,
	F,
	G,
	H,
	I,
	J,
	K,
	L,
	M,
	N,
	O,
	P,
	Q,
	R,
	S,
	T,
	U,
	V,
	W,
	X,
	Y,
	Z,

	Imag,

	Pi,
	Euler,
	Theta,

	Invalid
};

/* Number of symbols from Sym::A up to Sym::Invalid */
constexpr unsigned sym_count = static_cast<unsigned>(Sym::Invalid) - static_cast<unsigned>(Sym::A);

/*Wrapper functions for shorthand calling in functions*/
/*Expects null terminated string*/
mp_rat num_FromString(const char *str);
mp_rat num_FromInt(mp_small num);
mp_rat num_FromFraction(mp_small num, mp_small den);
mp_rat num_Copy(const mpq_t *other);
/*Expects num to be reduced*/
bool num_IsInt(const mpq_t *num, mp_small value);
int num_Compare(const mpq_t *a, const mpq_t *b);
char *num_ToString(const mpq_t *num, mp_size precision);
void num_Cleanup(mp_rat num);

template <class T> class ChildIterator {
  public:
	explicit ChildIterator(T *node) : node(node) {
	}

	T *operator*() const {
		return node;
	}

	ChildIterator &operator++() {
		node = node->next();
		return *this;
	}

	bool operator==(const ChildIterator &other) const = default;

  private:
	T *node;
};

template <class T> class ChildRange {
  public:
	explicit ChildRange(T *first) : first(first) {
	}

	ChildIterator<T> begin() const {
		return ChildIterator<T>(first);
	}

	ChildIterator<T> end() const {
		return ChildIterator<T>(nullptr);
	}

  private:
	T *first;
};

class ast {
  public:
	enum class Type : unsigned char { Number, Symbol, Operator };

	static constexpr size_t npos = static_cast<size_t>(-1);

	static void *operator new(size_t size) {
		return malloc(size);
	}

	static void operator delete(void *node) {
		free(node);
	}

	// make and dispose exist solely because without them a lot of garbage is inlined.
	// this saves about 21.5 KB of flash space!
	/* Allocates a node, which dispose frees */
	static ast *make(mp_rat num);
	static ast *make(Sym symbol);
	static ast *make(Op op);
	static ast *make(Op op, ast *operand);
	static ast *make(Op op, ast *left, ast *right);
	/* Frees node and its children, doing nothing if node is null */
	static void dispose(ast *node);

	/*Copies are not linked to any siblings*/
	ast(const ast &other);
	ast(ast &&other) noexcept;
	/*Assignment replaces the contents and keeps the node's place among its siblings*/
	ast &operator=(const ast &other);
	ast &operator=(ast &&other) noexcept;
	~ast();

	/* Takes the contents of replacement, which may be a child of this node, and deletes it */
	void replace(ast *replacement);

	ast *copy() const;
	/*Children of commutative operators may be in any order*/
	bool compare(const ast &other) const;
	bool operator==(const ast &other) const;

	Type type() const {
		return type_;
	}

	constexpr bool isNumber() const {
		return type_ == Type::Number;
	}

	constexpr bool isSymbol() const {
		return type_ == Type::Symbol;
	}

	constexpr bool isOperator() const {
		return type_ == Type::Operator;
	}

	/* Expects the number to be reduced */
	bool isInt(mp_small value) const {
		return isNumber() && num_IsInt(num_, value);
	}

	/* Null when the node is not a number */
	mp_rat num() {
		return isNumber() ? num_ : nullptr;
	}

	const mpq_t *num() const {
		return isNumber() ? num_ : nullptr;
	}

	/* Sym::Invalid when the node is not a symbol */
	Sym symbol() const {
		return isSymbol() ? symbol_ : Sym::Invalid;
	}

	constexpr bool isOp(Op op) const {
		return isOperator() && op_.type == op;
	}

	/* Op::Amount when the node is not an operator */
	Op op() const {
		return isOperator() ? op_.type : Op::Amount;
	}

	/* Expects an operator node */
	void setOp(Op op) {
		op_.type = op;
	}

	ast *next() {
		return next_;
	}

	const ast *next() const {
		return next_;
	}

	ast *firstChild() {
		return isOperator() ? op_.content : nullptr;
	}

	const ast *firstChild() const {
		return isOperator() ? op_.content : nullptr;
	}

	ChildRange<ast> children() {
		return ChildRange<ast>(firstChild());
	}

	ChildRange<const ast> children() const {
		return ChildRange<const ast>(firstChild());
	}

	ast *lastChild();
	const ast *lastChild() const;
	ast *childAt(size_t index);
	const ast *childAt(size_t index) const;
	size_t childCount() const;
	/*npos when child is not a child of this node*/
	size_t indexOf(const ast &child) const;

	/*The node takes ownership of child*/
	Error appendChild(ast *child);
	Error insertChild(ast *child, size_t index);

	/*Moves the children of other to the end of this node's children*/
	void takeChildren(ast &other);

	/*The caller takes ownership of the removed child, which is null if there is none*/
	ast *removeChild(const ast &child);
	ast *removeChildAt(size_t index);

  private:
	struct Operator {
		Op type;
		/* The base node for the linked list */
		ast *content;
	};

	Type type_;
	/* For the linked list implementation */
	ast *next_ = nullptr;

	union {
		mp_rat num_;
		Sym symbol_;
		Operator op_;
	};

	/* Leaves the contents for make to set */
	explicit ast(Type type) : type_(type) {
	}

	/* Expects this node to own nothing, and leaves other owning nothing */
	void takeContents(ast &other);
	static ast *unlink(ast **link);
};
