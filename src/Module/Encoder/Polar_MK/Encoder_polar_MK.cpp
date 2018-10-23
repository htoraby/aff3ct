#include <vector>
#include <cmath>
#include <sstream>

#include "Tools/Exception/exception.hpp"
#include "Tools/Math/utils.h"

#include "Encoder_polar_MK.hpp"

using namespace aff3ct;
using namespace aff3ct::module;

// DEBUG
template <typename T = int32_t>
void display_matrix(const std::vector<std::vector<T>>& M)
{
	for (auto row = 0; row < (int)M.size(); row++)
	{
		for (auto col = 0; col < (int)M[0].size(); col++)
		{
			std::cout << M[row][col] << "\t";
		}
		std::cout << std::endl;
	}
}

template <typename T = int32_t>
void kronecker_product(const std::vector<std::vector<T>>& A,
                       const std::vector<std::vector<T>>& B,
                             std::vector<std::vector<T>>& C)
{
	for (auto row_A = 0; row_A < (int)A.size(); row_A++)
		for (auto col_A = 0; col_A < (int)A[0].size(); col_A++)
			for (auto row_B = 0; row_B < (int)B.size(); row_B++)
				for (auto col_B = 0; col_B < (int)B[0].size(); col_B++)
					C[row_A * B.size() + row_B][col_A * B[0].size() + col_B] = A[row_A][col_A] * B[row_B][col_B];
}

template <typename T = int32_t>
std::vector<std::vector<T>> kronecker_product(const std::vector<std::vector<T>>& A,
                                              const std::vector<std::vector<T>>& B)
{
	// verifications --------------------------------------------------------------------------------------------------
	if (A.size() == 0)
	{
		std::stringstream message;
		message << "'A.size()' should be higher than 0 ('A.size()' = " << A.size() << ").";
		throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
	}

	if (B.size() == 0)
	{
		std::stringstream message;
		message << "'B.size()' should be higher than 0 ('B.size()' = " << B.size() << ").";
		throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
	}

	for (auto l = 0; l < (int)A.size(); l++)
	{
		if (A[l].size() != A.size())
		{
			std::stringstream message;
			message << "'A[l].size()' has to be equal to 'A.size()' ('l' = " << l
			        << ", 'A[l].size()' = " << A[l].size()
			        << ", 'A.size()' = " << A.size() << ").";
			throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
		}
	}

	for (auto l = 0; l < (int)B.size(); l++)
	{
		if (B[l].size() != B.size())
		{
			std::stringstream message;
			message << "'B[l].size()' has to be equal to 'B.size()' ('l' = " << l
			        << ", 'B[l].size()' = " << B[l].size()
			        << ", 'B.size()' = " << B.size() << ").";
			throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
		}
	}
	// ----------------------------------------------------------------------------------------------------------------

	std::vector<std::vector<T>> C(A.size() * B.size(), std::vector<T>(A[0].size() * B[0].size()));
	kronecker_product(A, B, C);
	return C;
}

template <typename B>
Encoder_polar_MK<B>
::Encoder_polar_MK(const int& K, const int& N, const std::vector<bool>& frozen_bits,
                   const std::vector<std::vector<bool>>& kernel_matrix, const int n_frames)
: Encoder<B>(K, N, n_frames),
  bp(kernel_matrix.size()),
  m((int)(std::log(N)/std::log(bp))),
  frozen_bits(frozen_bits),
  kernel_matrix(kernel_matrix),
  X_N_tmp(this->N),
  Ke(kernel_matrix.size() * kernel_matrix.size()),
  idx(kernel_matrix.size())
{
	const std::string name = "Encoder_polar_MK";
	this->set_name(name);
	this->set_sys(false);

	if (this->N != (int)frozen_bits.size())
	{
		std::stringstream message;
		message << "'frozen_bits.size()' has to be equal to 'N' ('frozen_bits.size()' = " << frozen_bits.size()
		        << ", 'N' = " << N << ").";
		throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
	}

	auto k = 0; for (auto i = 0; i < this->N; i++) if (frozen_bits[i] == 0) k++;
	if (this->K != k)
	{
		std::stringstream message;
		message << "The number of information bits in the frozen_bits is invalid ('K' = " << K << ", 'k' = "
		        << k << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}

	if (!tools::is_power((int)kernel_matrix.size(), N))
	{
		std::stringstream message;
		message << "'kernel_matrix.size()' has to be a power of 'N' ('kernel_matrix.size()' = " << kernel_matrix.size()
		        << ", 'N' = " << N << ").";
		throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
	}

	for (auto l = 0; l < (int)kernel_matrix.size(); l++)
	{
		if (kernel_matrix[l].size() != kernel_matrix.size())
		{
			std::stringstream message;
			message << "'kernel_matrix[l].size()' has to be equal to 'kernel_matrix.size()' ('l' = " << l
			        << ", 'kernel_matrix[l].size()' = " << kernel_matrix[l].size()
			        << ", 'kernel_matrix.size()' = " << kernel_matrix.size() << ").";
			throw tools::length_error(__FILE__, __LINE__, __func__, message.str());
		}
	}

	for (auto i = 0; i < (int)this->kernel_matrix.size(); i++)
		for (auto j = 0; j < (int)this->kernel_matrix.size(); j++)
			this->Ke[i * (int)this->kernel_matrix.size() +j] = (int8_t)this->kernel_matrix[j][i];

	this->notify_frozenbits_update();
}

template <typename B>
void Encoder_polar_MK<B>
::_encode(const B *U_K, B *X_N, const int frame_id)
{
	this->convert(U_K, X_N);
	this->light_encode(X_N);
}

template <typename B>
void kernel(const B *u, const uint32_t *idx, const int8_t *Ke, B *x, const int size)
{
	for (auto i = 0; i < size; i++)
	{
		const auto stride = i * size;
		auto sum = 0;
		for (auto j = 0; j < size; j++)
			sum += u[idx[j]] & Ke[stride +j];
		x[idx[i]] = sum & (int8_t)1;
	}
}

template <typename B>
void Encoder_polar_MK<B>
::light_encode(B *X_N)
{
	const auto kernel_size = (int)this->kernel_matrix.size();

	for (auto s = 0; s < this->m; s++)
	{
		const auto block_size = (int)std::pow((float)kernel_size, s);
		const auto n_blocks = this->N / (block_size * kernel_size);

		for (auto b = 0; b < n_blocks; b++)
		{
			const auto n_kernels = block_size;
			for (auto k = 0; k < n_kernels; k++)
			{
				for (auto i = 0; i < kernel_size; i++)
					this->idx[i] = (uint32_t)(b * block_size * kernel_size + block_size * i +k);

				const auto off_out = b * block_size * kernel_size + k * kernel_size;
				kernel(X_N, this->idx.data(), this->Ke.data(), X_N, kernel_size);
			}
		}
	}
}

template <typename B>
void Encoder_polar_MK<B>
::convert(const B *U_K, B *U_N)
{
	if (U_K == U_N)
	{
		std::vector<B> U_K_tmp(this->K);
		std::copy(U_K, U_K + this->K, U_K_tmp.begin());

		auto j = 0;
		for (unsigned i = 0; i < frozen_bits.size(); i++)
			U_N[i] = (frozen_bits[i]) ? (B)0 : U_K_tmp[j++];
	}
	else
	{
		auto j = 0;
		for (unsigned i = 0; i < frozen_bits.size(); i++)
			U_N[i] = (frozen_bits[i]) ? (B)0 : U_K[j++];
	}
}

// template <typename B>
// bool Encoder_polar_MK<B>
// ::is_codeword(const B *X_N)
// {
// 	std::copy(X_N, X_N + this->N, this->X_N_tmp.data());

// 	for (auto k = (this->N >> 1); k > 0; k >>= 1)
// 		for (auto j = 0; j < this->N; j += 2 * k)
// 		{
// 			for (auto i = 0; i < k; i++)
// 				this->X_N_tmp[j + i] = this->X_N_tmp[j + i] ^ this->X_N_tmp[k + j + i];

// 			if (this->frozen_bits[j + k -1] && this->X_N_tmp[j + k -1])
// 				return false;
// 		}

// 	return true;
// }

template <typename B>
void Encoder_polar_MK<B>
::notify_frozenbits_update()
{
	auto k = 0;
	for (auto n = 0; n < this->N; n++)
		if (!frozen_bits[n])
			this->info_bits_pos[k++] = n;
}

// ==================================================================================== explicit template instantiation
#include "Tools/types.h"
#ifdef MULTI_PREC
template class aff3ct::module::Encoder_polar_MK<B_8>;
template class aff3ct::module::Encoder_polar_MK<B_16>;
template class aff3ct::module::Encoder_polar_MK<B_32>;
template class aff3ct::module::Encoder_polar_MK<B_64>;
#else
template class aff3ct::module::Encoder_polar_MK<B>;
#endif
// ==================================================================================== explicit template instantiation
