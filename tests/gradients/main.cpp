#include <cassert>

#include "tensor.hpp"
#include "autograd.hpp"

// TODO: put into a util
bool buffer_cheq(const Resource &A, const Resource &B)
{
	if (A.elements != B.elements)
		return false;

	for (size_t i = 0; i < A.elements; i++) {
		if (A.ptr[i] != B.ptr[i])
			return false;
	}

	return true;
}

void test_square()
{
	Tape tape;
	Tensor X = Tensor::randn({ 3, 3 });
	Tensor S = ops::square.forward(X);
	Tensor dS = Tensor::ones_like(S);
	Tensor dX = ops::square.pullback_args({ X }, dS, tape)[0];

	// TODO: print only first time, except for the failed case
	// then run experiments again
	fmt::print("X: {}\nS: {}\ndS: {}\ndX: {}\n", X, S, dS, dX);

	Tensor gt_dX = 2 * X;
	fmt::print("gt dX: {}\n", gt_dX);

	// TODO: buffer check here
	assert(buffer_cheq(dX.buffer, gt_dX.buffer));
}

int main()
{
	test_square();
}