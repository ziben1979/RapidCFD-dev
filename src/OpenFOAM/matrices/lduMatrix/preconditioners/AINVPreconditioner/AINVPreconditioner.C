#include "AINVPreconditioner.H"

namespace Foam
{
    defineTypeNameAndDebug(AINVPreconditioner, 0);

    lduMatrix::preconditioner::
        addsymMatrixConstructorToTable<AINVPreconditioner>
        addAINVPreconditionerSymMatrixConstructorToTable_;

    lduMatrix::preconditioner::
        addasymMatrixConstructorToTable<AINVPreconditioner>
        addAINVPreconditionerAsymMatrixConstructorToTable_;


    #define MAX_NEI_SIZE 3
	
    template<bool normalMult>
    struct AINVPreconditionerFunctor 
    {
        textures<scalar> psi;
        textures<scalar> rD;
        const scalar* lower;
        const scalar* upper;
        const label* own;
        const label* nei;
        const label* losort;
        const label* ownStart;
        const label* losortStart;

        AINVPreconditionerFunctor
        (
            textures<scalar> _psi, 
            textures<scalar> _rD, 
            const scalar* _lower,
            const scalar* _upper,
            const label* _own,
            const label* _nei,
            const label* _losort,
            const label* _ownStart,
            const label* _losortStart
        ):
             psi(_psi),
             rD(_rD),
             lower(_lower),
             upper(_upper),
             own(_own),
             nei(_nei),
             losort(_losort),
             ownStart(_ownStart),
             losortStart(_losortStart)
        {}

        __HOST____DEVICE__
        scalar operator()(const label& id)
        {
            scalar out = 0;
            scalar tmpSum[2*MAX_NEI_SIZE] = {};
            
            label oStart = ownStart[id];
            label oSize = ownStart[id+1] - oStart;
            
            label nStart = losortStart[id];
            label nSize = losortStart[id+1] - nStart;

            for(label i = 0; i<MAX_NEI_SIZE; i++)
            {
                if(i<oSize)
                {
                    label face = oStart + i;
                    if(normalMult)
                        tmpSum[i] = upper[face]*rD[nei[face]]*psi[nei[face]];
                    else
                        tmpSum[i] = upper[face]*rD[nei[face]]*psi[nei[face]];
                }
            }

            for(label i = 0; i<MAX_NEI_SIZE; i++)
            {
                if(i<nSize)
                {
                    label face = losort[nStart + i];
                    if(normalMult)
                        tmpSum[i+MAX_NEI_SIZE] = lower[face]*rD[own[face]]*psi[own[face]]; 
                    else
                        tmpSum[i+MAX_NEI_SIZE] = upper[face]*rD[own[face]]*psi[own[face]]; 
                }
            }

            for(label i = 0; i<2*MAX_NEI_SIZE; i++)
            {
                out+= tmpSum[i]; 
            }
            
            for(label i = MAX_NEI_SIZE; i<oSize; i++)
            {
                label face = oStart + i;
                if(normalMult)
                    out += upper[face]*rD[nei[face]]*psi[nei[face]];
                else
                    out += lower[face]*rD[nei[face]]*psi[nei[face]];
            }
            
            
            for(label i = MAX_NEI_SIZE; i<nSize; i++)
            {
                 label face = losort[nStart + i];
                 if(normalMult)
                     out += lower[face]*rD[own[face]]*psi[own[face]];
                 else
                     out += upper[face]*rD[own[face]]*psi[own[face]];
            }

            
            return rD[id]*(psi[id]-out);
        }
    };
    
    #undef MAX_NEI_SIZE
}

Foam::AINVPreconditioner::AINVPreconditioner
(
    const lduMatrix::solver& sol,
    const dictionary&
)
:
    lduMatrix::preconditioner(sol),
    rD(sol.matrix().diag().size()),
    rDTex(rD.size(),rD.data())
{ 
    const scalargpuField& Diag = solver_.matrix().diag();

    thrust::transform
    (
        Diag.begin(),
        Diag.end(),
        rD.begin(),
        divideOperatorSFFunctor<scalar,scalar,scalar>(1.0)
    );
}

Foam::AINVPreconditioner::~AINVPreconditioner()
{
   rDTex.destroy();
}

template<bool normalMult>
void Foam::AINVPreconditioner::preconditionImpl
(
    scalargpuField& w,
    const scalargpuField& r,
    const direction d
) const
{
    const labelgpuList& l = solver_.matrix().lduAddr().lowerAddr();
    const labelgpuList& u = solver_.matrix().lduAddr().upperAddr();
    const labelgpuList& losort = solver_.matrix().lduAddr().losortAddr();

    const labelgpuList& ownStart = solver_.matrix().lduAddr().ownerStartAddr();
    const labelgpuList& losortStart = solver_.matrix().lduAddr().losortStartAddr();

    const scalargpuField& Lower = solver_.matrix().lower();
    const scalargpuField& Upper = solver_.matrix().upper();

    textures<scalar> rTex(r);

    thrust::transform
    (
        thrust::make_counting_iterator(0),
        thrust::make_counting_iterator(0)+r.size(),
        w.begin(),
        AINVPreconditionerFunctor<normalMult>
        (
            rTex,
            rDTex,
            Lower.data(),
            Upper.data(),
            l.data(),
            u.data(),
            losort.data(),
            ownStart.data(),
            losortStart.data()
        )
    );

    rTex.destroy();
}
