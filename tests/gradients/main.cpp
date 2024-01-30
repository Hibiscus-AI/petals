#include <functional>

#include <fmt/color.h>

#include "composition.hpp"
#include "tensor.hpp"
#include "autograd.hpp"
#include "ops.hpp"

// TODO: use only f64 buffers for gradient checking
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

bool buffer_close(const Resource &A, const Resource &B, float tolerance)
{
	if (A.elements != B.elements)
		return false;

	for (size_t i = 0; i < A.elements; i++) {
		if (std::abs(A.ptr[i] - B.ptr[i]) > tolerance) {
			fmt::print("delta is {} > {}\n", std::abs(A.ptr[i] - B.ptr[i]), tolerance);
			return false;
		}
	}

	return true;
}

// TODO: stress test arbitary shapes...

// Generic function pullback checking
// TODO: arbitrary shapes, number of args, etc.
bool check_pullback(const std::function <DynamicDeferred (const Tensor &)> &f)
{
	constexpr float epsilon = 1e-2f;

	// TODO: sum the output with a random similar tensor to do the checking...
	const Shape shape { 3, 3 };

	Tape tape;
	Tensor X = Tensor::randn(shape) - 0.5;

	auto Y = f(X);
	Tensor eY = Y;

	// Tensor dY = Tensor::ones({});
	Tensor dY = Tensor::randn({}) - 0.5;
	Tensor dX = Y.pullback(dY, tape)[0];

	Tensor gt_dX = Tensor::zeros_like(dX);
	for (size_t i = 0; i < gt_dX.buffer.elements; i++) {
		Tensor pX = X.clone();
		pX.buffer.ptr[i] += epsilon;
		Tensor pY = f(pX);

		Tensor nX = X.clone();
		nX.buffer.ptr[i] -= epsilon;
		Tensor nY = f(nX);

		float dw = (pY.buffer.ptr[0] - nY.buffer.ptr[0])/(2 * epsilon);
		gt_dX.buffer.ptr[i] = dw * dY.buffer.ptr[0];
	}


	fmt::print("\nX: {} -> Y: {}, dY: {}\n", X, eY, dY);
	fmt::print("AD dX: {}\n", dX);
	fmt::print("FD dX: {}\n", gt_dX);

	return buffer_close(dX.buffer, gt_dX.buffer, epsilon * epsilon);
}

bool test_square()
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
	return buffer_cheq(dX.buffer, gt_dX.buffer);
}

// TODO: gradient checking with tape and lamda
// TODO: delta checking on arbitrary functions
bool test_linear()
{
	constexpr size_t WIDTH = 10;
	constexpr size_t HEIGHT = 15;
	constexpr size_t BATCH = 20;

	constexpr float epsilon = 1e-2f;

	Tensor X = Tensor::randn({ BATCH, WIDTH });
	Tensor Y = Tensor::randn({ BATCH, HEIGHT });
	Linear linear = Linear::from(WIDTH, HEIGHT);

	// Gradient checking on the parameters
	Tensor gt_dW = Tensor::zeros_like(linear.W);
	for (size_t i = 0; i < gt_dW.buffer.elements; i++) {
		// epsilon+
		Linear p_linear = linear;
		p_linear.W = linear.W.clone();
		p_linear.W.buffer.ptr[i] += epsilon;

		Tensor p_out = sum(square(p_linear.forward(X) - Y));

		// epsilon-
		Linear n_linear = linear;
		n_linear.W = linear.W.clone();
		n_linear.W.buffer.ptr[i] -= epsilon;

		Tensor n_out = sum(square(n_linear.forward(X) - Y));

		float dw = (p_out.buffer.ptr[0] - n_out.buffer.ptr[0])/(2 * epsilon);
		gt_dW.buffer.ptr[i] = dw;
	}

	fmt::print("FD dW = {}\n", gt_dW);

	Tape tape = Tape::from({ &linear.W });
	auto lY = linear.forward(X);
	auto loss = sum(square(lY - Y));
	loss.eval();

	auto deltas = loss.backward(tape);
	linear.pullback_args({ X }, deltas[0], tape);

	Tensor dW = tape[linear.W.tag];
	fmt::print("AD dW = {}\n", dW);

	return buffer_close(dW.buffer, gt_dW.buffer, epsilon * epsilon);
}

// TODO: generic gradient checking for parameters
bool test_dnn()
{
	constexpr size_t WIDTH  = 4;
	constexpr size_t HEIGHT = 4;
	constexpr size_t BATCH  = 2;

	constexpr float epsilon = 1e-2f;

	Tensor X = Tensor::randn({ BATCH, WIDTH });
	Tensor Y = Tensor::randn({ BATCH, HEIGHT });
	Linear linear1 = Linear::from(WIDTH, 2 * HEIGHT);
	Linear linear2 = Linear::from(2 * HEIGHT, HEIGHT);

	// Gradient checking the second layer
	{
		Tensor gt_dW = Tensor::zeros_like(linear2.W);
		for (size_t i = 0; i < gt_dW.buffer.elements; i++) {
			// epsilon+
			Linear p_linear = linear2;
			p_linear.W = linear2.W.clone();
			p_linear.W.buffer.ptr[i] += epsilon;

			Tensor l1_out = linear1.forward(X);
			Tensor relu_out = ops::relu.forward(l1_out);
			Tensor l2_out = p_linear.forward(relu_out);
			Tensor p_out = sum(square(l2_out - Y));

			// epsilon-
			Linear n_linear = linear2;
			n_linear.W = linear2.W.clone();
			n_linear.W.buffer.ptr[i] -= epsilon;

			l1_out = linear1.forward(X);
			relu_out = ops::relu.forward(l1_out);
			l2_out = n_linear.forward(relu_out);
			Tensor n_out = sum(square(l2_out - Y));

			float dw = (p_out.buffer.ptr[0] - n_out.buffer.ptr[0])/(2 * epsilon);
			gt_dW.buffer.ptr[i] = dw;
		}

		fmt::print("FD dW = {}\n", gt_dW);

		Tape tape = Tape::from({ &linear2.W });
		Tensor l1_out = linear1.forward(X);
		Tensor relu_out = ops::relu.forward(l1_out);
		Tensor lY = linear2.forward(relu_out);
		auto loss = sum(square(lY - Y));
		loss.eval();

		Tensor delta_linear2 = loss.backward(tape)[0];
		Tensor delta_relu = linear2.pullback_args({ relu_out }, delta_linear2, tape)[0];
		Tensor delta_linear1 = ops::relu.pullback_args({ l1_out }, delta_relu, tape)[0];
		Tensor delta_out = linear1.pullback_args({ X }, delta_linear1, tape)[0];

		Tensor dW = tape[linear2.W.tag];
		fmt::print("AD dW = {}\n", dW);
	}

	// return buffer_close(dW.buffer, gt_dW.buffer, epsilon * epsilon);

	// Gradient checking the first layer
	{
		Tensor gt_dW = Tensor::zeros_like(linear1.W);
		for (size_t i = 0; i < gt_dW.buffer.elements; i++) {
			// epsilon+
			Linear p_linear = linear1;
			p_linear.W = linear1.W.clone();
			p_linear.W.buffer.ptr[i] += epsilon;

			Tensor l1_out = p_linear.forward(X);
			Tensor relu_out = ops::relu.forward(l1_out);
			Tensor l2_out = linear2.forward(relu_out);
			Tensor p_out = sum(square(l2_out - Y));

			// epsilon-
			Linear n_linear = linear1;
			n_linear.W = linear1.W.clone();
			n_linear.W.buffer.ptr[i] -= epsilon;

			l1_out = n_linear.forward(X);
			relu_out = ops::relu.forward(l1_out);
			l2_out = linear2.forward(relu_out);
			Tensor n_out = sum(square(l2_out - Y));

			float dw = (p_out.buffer.ptr[0] - n_out.buffer.ptr[0])/(2 * epsilon);
			gt_dW.buffer.ptr[i] = dw;
		}

		fmt::print("\n\nFD dW = {}\n", gt_dW);

		Tape tape = Tape::from({ &linear1.W });
		Tensor l1_out = linear1.forward(X);
		Tensor relu_out = ops::relu.forward(l1_out);
		Tensor lY = linear2.forward(relu_out);
		auto loss = sum(square(lY - Y));
		loss.eval();

		Tensor delta_linear2 = loss.backward(tape)[0];
		Tensor delta_relu = linear2.pullback_args({ relu_out }, delta_linear2, tape)[0];
		Tensor delta_linear1 = ops::relu.pullback_args({ l1_out }, delta_relu, tape)[0];
		Tensor delta_out = linear1.pullback_args({ X }, delta_linear1, tape)[0];

		Tensor dW = tape[linear1.W.tag];
		fmt::print("AD dW = {}\n", dW);
	}

	return true;
	// return buffer_close(dW.buffer, gt_dW.buffer, epsilon * epsilon);
}

void robust_test(const std::function <bool ()> &test, size_t iterations = 100)
{
	for (size_t i = 0; i < iterations; i++) {
		if (!test()) {
			fmt::print("test failed\n");
			break;
		}
	}
}

int main()
{
	check_pullback([](const Tensor &X) { return sum(X); });
	check_pullback([](const Tensor &X) { return sum(square(X)); });
	check_pullback([](const Tensor &X) { return sum(relu(X)); });
	check_pullback([](const Tensor &X) { return sum(sigmoid(X)); });
	check_pullback([](const Tensor &X) { return sum(softmax(X)); });

	struct LinearTester {
		Linear linear = Linear::from(3, 5);

		DynamicDeferred operator()(const Tensor &X) {
			return sum(DynamicDeferred::from(nop_ptr(&linear), { X }));
		}
	};

	check_pullback(LinearTester {});
	test_dnn();

	// robust_test(test_square);
	// robust_test(test_linear);
	// test_linear();
}
