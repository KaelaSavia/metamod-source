/* ======== SourceHook ========
* Copyright (C) 2004-2005 Metamod:Source Development Team
* No warranties of any kind
*
* License: zlib/libpng
*
* Author(s): Pavol "PM OnoTo" Marko
* ============================
*/

/**
 * @brief This file provides a way for getting information about a member function.
 * @file sh_memfuncinfo.h
 */

#ifndef __SHINT_MEMFUNC_INFO_H__
#define __SHINT_MEMFUNC_INFO_H__

namespace SourceHook
{

	// Don Clugston:
	//		implicit_cast< >
	// I believe this was originally going to be in the C++ standard but 
	// was left out by accident. It's even milder than static_cast.
	// I use it instead of static_cast<> to emphasize that I'm not doing
	// anything nasty. 
	// Usage is identical to static_cast<>
	template <class OutputClass, class InputClass>
	inline OutputClass implicit_cast(InputClass input){
		return input;
	}


	struct MemFuncInfo
	{
		bool isVirtual;		// Is the function virtual?
		int thisptroffs;	// The this pointer the function expects to be called with
							// If -1, you need to call the GetFuncInfo_GetThisPtr function
		int vtblindex;		// The function's index in the vtable (0-based, 1=second entry, 2=third entry, ...)
		int vtbloffs;		// The vtable pointer
	};

	// Ideas by Don Clugston.
	// Check out his excellent paper: http://www.codeproject.com/cpp/FastDelegate.asp

	template<int N> struct MFI_Impl
	{
		template<class MFP> static inline void GetFuncInfo(MFP *mfp, MemFuncInfo &out)
		{
			static char weird_memfunc_pointer_exclamation_mark_arrow_error[N-1000];
		}
	}; 

# if SH_COMP == SH_COMP_GCC

	template<> struct MFI_Impl<8>   // All of these have size==8
	{
		struct GCC_MemFunPtr
		{
			union
			{
				void *funcadr;				// always even
				int vtable_index_plus1;		//  = vindex+1, always odd
			};
			int delta;
		};
		template<class MFP> static inline void GetFuncInfo(MFP mfp, MemFuncInfo &out)
		{
			GCC_MemFunPtr *mfp_detail = (GCC_MemFunPtr*)&mfp;
			out.thisptroffs = mfp_detail->delta;
			if (mfp_detail->vtable_index_plus1 & 1)
			{
				out.vtblindex = (mfp_detail->vtable_index_plus1 - 1) / 4;
				out.vtbloffs = 0;
				out.isVirtual = true;
			}
			else
				out.isVirtual = false;
		}
	}; 

# elif SH_COMP == SH_COMP_MSVC

	namespace
	{
		int MFI_GetVtblOffset(void *mfp)
		{
			unsigned char *addr = (unsigned char*)mfp;
			if (*addr == 0xE9)		// Jmp
			{
				// May or may not be!
				// Check where it'd jump
				addr += 5 /*size of the instruction*/ + *(unsigned long*)(addr + 1);
			}
	
			// Check whether it's a virtual function call
			// They look like this:
			// 004125A0 8B 01            mov         eax,dword ptr [ecx] 
			// 004125A2 FF 60 04         jmp         dword ptr [eax+4]
			//		==OR==
			// 00411B80 8B 01            mov         eax,dword ptr [ecx] 
			// 00411B82 FF A0 18 03 00 00 jmp         dword ptr [eax+318h] 
			if (*addr++ == 0x8B && *addr++ == 0x01 && *addr++ == 0xFF)
			{
				if (*addr == 0x60)
				{
					return *++addr / 4;
				}
				else if (*addr == 0xA0)
				{
					return *((unsigned int*)++addr) / 4;
				}
				else if (*addr == 0x20)
					return 0;
				else
					return -1;
			}
			return -1;
		}
	}

	template<> struct MFI_Impl<4>   // simple ones
	{
		template<class MFP> static inline void GetFuncInfo(MFP mfp, MemFuncInfo &out)
		{
			out.vtblindex = MFI_GetVtblOffset(*(void**)&mfp);
			out.isVirtual = out.vtblindex >= 0 ? true : false;
			out.thisptroffs = 0;
			out.vtbloffs = 0;
		}
	};

	template<> struct MFI_Impl<8>   // more complicated ones!
	{
		struct MSVC_MemFunPtr2
		{
			void *funcadr;
			int delta;
		};
		template<class MFP> static inline void GetFuncInfo(MFP mfp, MemFuncInfo &out)
		{
			out.vtblindex = MFI_GetVtblOffset(*(void**)&mfp);
			out.isVirtual = out.vtblindex >= 0 ? true : false;
			out.thisptroffs = reinterpret_cast<MSVC_MemFunPtr2*>(&mfp)->delta;
			out.vtbloffs = 0;
		}
	};

	// By Don Clugston, adapted
	template<> struct MFI_Impl<12>   // WOW IT"S GETTING BIGGER OMGOMOGMG
	{
		class __single_inheritance GenericClass;
		class GenericClass {};

		struct MicrosoftVirtualMFP {
			void (GenericClass::*codeptr)(); // points to the actual member function
			int delta;		// #bytes to be added to the 'this' pointer
			int vtable_index; // or 0 if no virtual inheritance
		};

		struct GenericVirtualClass : virtual public GenericClass
		{
			typedef GenericVirtualClass * (GenericVirtualClass::*ProbePtrType)();
			GenericVirtualClass * GetThis() { return this; }
		};

		template<class MFP> static inline void GetFuncInfo(MFP mfp, MemFuncInfo &out)
		{
			out.vtblindex = MFI_GetVtblOffset(*(void**)&mfp);
			out.isVirtual = out.vtblindex >= 0 ? true : false;
			// This pointer
			/*
			union {
				MFP func;
				GenericClass* (T::*ProbeFunc)();
				MicrosoftVirtualMFP s;
			} u;
			u.func = mfp;
			union {
				GenericVirtualClass::ProbePtrType virtfunc;
				MicrosoftVirtualMFP s;
			} u2;

			// Check that the horrible_cast<>s will work
			typedef int ERROR_CantUsehorrible_cast[sizeof(mfp)==sizeof(u.s)
				&& sizeof(mfp)==sizeof(u.ProbeFunc)
				&& sizeof(u2.virtfunc)==sizeof(u2.s) ? 1 : -1];
			// Unfortunately, taking the address of a MF prevents it from being inlined, so 
			// this next line can't be completely optimised away by the compiler.
			u2.virtfunc = &GenericVirtualClass::GetThis;
			u.s.codeptr = u2.s.codeptr;
			out.thisptroffs = (reinterpret_cast<T*>(NULL)->*u.ProbeFunc)();
			*/
			out.thisptroffs = -1;
			out.vtbloffs = 0;
		}
	};

	// Don: Nasty hack for Microsoft and Intel (IA32 and Itanium)
	// unknown_inheritance classes go here 
	// This is probably the ugliest bit of code I've ever written. Look at the casts!
	// There is a compiler bug in MSVC6 which prevents it from using this code.
	template<> struct MFI_Impl<16>   // THE BIGGEST ONE!!!1GABEN
	{
		template<class MFP> static inline void GetFuncInfo(MFP mfp, MemFuncInfo &out)
		{
			out.vtblindex = MFI_GetVtblOffset(*(void**)&mfp);
			out.isVirtual = out.vtblindex >= 0 ? true : false;

			// The member function pointer is 16 bytes long. We can't use a normal cast, but
			// we can use a union to do the conversion.
			union {
				MFP func;
				// In VC++ and ICL, an unknown_inheritance member pointer 
				// is internally defined as:
				struct {
					void *m_funcaddress; // points to the actual member function
					int delta;		// #bytes to be added to the 'this' pointer
					int vtordisp;		// #bytes to add to 'this' to find the vtable
					int vtable_index; // or 0 if no virtual inheritance
				} s;
			} u;
			// Check that the horrible_cast will work
			typedef int ERROR_CantUsehorrible_cast[sizeof(u.func)==sizeof(u.s)? 1 : -1];
			u.func = mfp;
			int virtual_delta = 0;
			if (u.s.vtable_index) { // Virtual inheritance is used
				/*
				// First, get to the vtable. 
				// It is 'vtordisp' bytes from the start of the class.
				int * vtable = *reinterpret_cast<int **>(
					reinterpret_cast<char *>(thisptr) + u.s.vtordisp );

				// 'vtable_index' tells us where in the table we should be looking.
				virtual_delta = u.s.vtordisp + *reinterpret_cast<const int *>( 
					reinterpret_cast<const char *>(vtable) + u.s.vtable_index);
			// The int at 'virtual_delta' gives us the amount to add to 'this'.
			// Finally we can add the three components together. Phew!
			out.thisptr = reinterpret_cast<void *>(
				reinterpret_cast<char *>(thisptr) + u.s.delta + virtual_delta);
				*/
				out.vtbloffs = u.s.vtordisp;
				out.thisptroffs = -1;
			}
			else
			{
				out.vtbloffs = out.vtblindex < 0 ? 0 : u.s.delta;
				out.thisptroffs = u.s.delta;
			}
		};
	};
# else
#  error Unsupported compiler
# endif

	template<class X> inline void GetFuncInfo(X mfp, MemFuncInfo &out)
	{
		MFI_Impl<sizeof(mfp)>::GetFuncInfo(mfp, out);
	}

}

#endif

