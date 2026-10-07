#ifndef COMPILE_PC

#include "calculus.hxx"

#include <stdio.h>

#include "../cas/cas.hxx"

Error calculus::run(
	Kind kind,
	ast **items,
	unsigned count,
	ast &respect_to,
	bool series,
	unsigned terms,
	char *summary
) {
	ast &e = *items[0];
	Error err = Error::Success;

	if (kind != calculus::Kind::DiffEq && count > 1)
		return Error::ParseBadComma;

	switch (kind) {
		case calculus::Kind::Derivative:
			simplify(e, Simp::Normalize | Simp::Commutative | Simp::Rational);
			derivative(e, respect_to, respect_to);
			break;
		case calculus::Kind::Integral:
			simplify(e, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval);
			integral_Indefinite(e, respect_to);
			break;
		case calculus::Kind::DiffEq: {
			DiffEq de;
			err = de.loadList(items, count, respect_to);

			if (err == Error::Success) {
				ast *solution;

				de.series = series;
				de.terms = terms;
				de.classify();
				err = de.solve(&solution);

				if (err == Error::Success) {
					if (summary != nullptr)
						sprintf(summary, "Solved. %s.", de.method);

					if (solution->firstChild()->compare(*de.y)) {
						e.replace(solution->firstChild()->next()->copy());
						ast::dispose(solution);
					} else {
						e.replace(solution);
					}
				} else if (err == Error::DeUnsolved || err == Error::DeIntegral) {
					if (summary != nullptr)
						sprintf(
							summary, "Order %u, %s. %s.", de.order, de.linear ? "linear" : "nonlinear", error_text(err)
						);

					if (de.linear)
						e.replace(ast::make(Op::Equals, de.standardForm(), de.g->copy()));

					err = Error::Success;
				}

				simplify_canonical_form(e, Canonical::All);
			}

			de.clear();
			return err;
		}
	}

	simplify(e, Simp::Normalize | Simp::Commutative | Simp::Rational | Simp::Eval | Simp::LikeTerms);
	simplify_canonical_form(e, Canonical::All);

	return err;
}

Error calculus::verify(ast **items, unsigned count, ast &respect_to, ast &solution, bool *satisfied) {
	DiffEq de;
	Error err = de.loadList(items, count, respect_to);

	if (err == Error::Success)
		err = de.verify(solution, satisfied);

	de.clear();

	return err;
}

#else
typedef int make_iso_compilers_happy;
#endif
