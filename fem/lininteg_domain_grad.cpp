// Copyright (c) 2010-2022, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "fem.hpp"
#include "../fem/kernels.hpp"
#include "../general/forall.hpp"

namespace mfem
{

template<int T_D1D = 0, int T_Q1D = 0> static
void DLFGradAssemble2D(const int vdim, const int ne, const int d, const int q,
                       const int *markers, const double *b, const double *g,
                       const double *jacobians,
                       const double *weights, const Vector &coeff, double *y)
{
   const auto F = coeff.Read();
   const auto M = Reshape(markers, ne);
   const auto B = Reshape(b, q, d);
   const auto G = Reshape(g, q, d);
   const auto J = Reshape(jacobians, q, q, 2,2, ne);
   const auto W = Reshape(weights, q, q);
   const bool cst = coeff.Size() == vdim*2;
   const auto C = cst ? Reshape(F,2,vdim,1,1,1) : Reshape(F,2,vdim,q,q,ne);
   auto Y = Reshape(y, d,d, vdim, ne);

   MFEM_FORALL_2D(e, ne, q, q, 1,
   {
      if (M(e) == 0) { return; } // ignore

      constexpr int Q = T_Q1D ? T_Q1D : MAX_Q1D;
      constexpr int D = T_D1D ? T_D1D : MAX_D1D;

      MFEM_SHARED double sBGt[2][Q*D];
      MFEM_SHARED double sQQ[2][Q*Q];
      MFEM_SHARED double sDQ[2][D*Q];

      const DeviceMatrix Bt(sBGt[0], q, d);
      const DeviceMatrix Gt(sBGt[1], q, d);
      kernels::internal::LoadBGt<D,Q>(d, q, B, G, sBGt);

      const DeviceMatrix QQ0(sQQ[0], q, q);
      const DeviceMatrix QQ1(sQQ[1], q, q);

      const DeviceMatrix DQ0(sDQ[0], d, q);
      const DeviceMatrix DQ1(sDQ[1], d, q);

      for (int c = 0; c < vdim; ++c)
      {
         const double cst_val0 = C(0,c,0,0,0);
         const double cst_val1 = C(1,c,0,0,0);

         MFEM_FOREACH_THREAD(x,x,q)
         {
            MFEM_FOREACH_THREAD(y,y,q)
            {
               const double w = W(x,y);
               const double J11 = J(x,y,0,0,e);
               const double J21 = J(x,y,1,0,e);
               const double J12 = J(x,y,0,1,e);
               const double J22 = J(x,y,1,1,e);
               const double u = cst ? cst_val0 : C(0,c,x,y,e);
               const double v = cst ? cst_val1 : C(1,c,x,y,e);
               // QQ = w * det(J) * J^{-1} . C = w * adj(J) . { u, v }
               QQ0(y,x) = w * (J22*u - J12*v);
               QQ1(y,x) = w * (J11*v - J21*u);
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(qx,x,q)
         {
            MFEM_FOREACH_THREAD(dy,y,d)
            {
               double u = 0.0, v = 0.0;
               for (int qy = 0; qy < q; ++qy)
               {
                  u += QQ0(qy,qx) * Bt(qy,dy);
                  v += QQ1(qy,qx) * Gt(qy,dy);
               }
               DQ0(dy,qx) = u;
               DQ1(dy,qx) = v;
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(dx,x,d)
         {
            MFEM_FOREACH_THREAD(dy,y,d)
            {
               double u = 0.0, v = 0.0;
               for (int qx = 0; qx < q; ++qx)
               {
                  u += DQ0(dy,qx) * Gt(qx,dx);
                  v += DQ1(dy,qx) * Bt(qx,dx);
               }
               Y(dx,dy,c,e) += u + v;
            }
         }
         MFEM_SYNC_THREAD;
      }
   });
}

template<int T_D1D = 0, int T_Q1D = 0> static
void DLFGradAssemble3D(const int vdim, const int ne, const int d, const int q,
                       const int *markers, const double *b, const double *g,
                       const double *jacobians,
                       const double *weights, const Vector &coeff,
                       double *output)
{
   const auto F = coeff.Read();
   const auto M = Reshape(markers, ne);
   const auto B = Reshape(b, q,d);
   const auto G = Reshape(g, q,d);
   const auto J = Reshape(jacobians, q,q,q, 3,3, ne);
   const auto W = Reshape(weights, q,q,q);
   const bool cst = coeff.Size() == vdim*3;
   const auto C = cst ? Reshape(F,3,vdim,1,1,1,1) : Reshape(F,3,vdim,q,q,q,ne);

   auto Y = Reshape(output, d,d,d, vdim, ne);

   MFEM_FORALL_2D(e, ne, q, q, 1,
   {
      if (M(e) == 0) { return; } // ignore

      constexpr int Q = T_Q1D ? T_Q1D : MAX_Q1D;
      constexpr int D = T_D1D ? T_D1D : MAX_D1D;

      double r_u[D];

      MFEM_SHARED double sBGt[2][Q*D];
      MFEM_SHARED double sQQQ[Q*Q*Q];

      const DeviceMatrix Bt(sBGt[0], q,d), Gt(sBGt[1], q,d);
      kernels::internal::LoadBGt<D,Q>(d,q,B,G,sBGt);

      const DeviceCube QQQ(sQQQ, q,q,q);
      const DeviceCube QQD(sQQQ, q,q,d);
      const DeviceCube QDD(sQQQ, q,d,d);

      for (int c = 0; c < vdim; ++c)
      {
         const double cst_val_0 = C(0,c,0,0,0,0);
         const double cst_val_1 = C(1,c,0,0,0,0);
         const double cst_val_2 = C(2,c,0,0,0,0);

         for (int k = 0; k < 3; ++k)
         {
            for (int z = 0; z < q; ++z)
            {
               MFEM_FOREACH_THREAD(y,y,q)
               {
                  MFEM_FOREACH_THREAD(x,x,q)
                  {
                     const double J11 = J(x,y,z,0,0,e);
                     const double J21 = J(x,y,z,1,0,e);
                     const double J31 = J(x,y,z,2,0,e);
                     const double J12 = J(x,y,z,0,1,e);
                     const double J22 = J(x,y,z,1,1,e);
                     const double J32 = J(x,y,z,2,1,e);
                     const double J13 = J(x,y,z,0,2,e);
                     const double J23 = J(x,y,z,1,2,e);
                     const double J33 = J(x,y,z,2,2,e);

                     const double u = cst ? cst_val_0 : C(0,c,x,y,z,e);
                     const double v = cst ? cst_val_1 : C(1,c,x,y,z,e);
                     const double w = cst ? cst_val_2 : C(2,c,x,y,z,e);

                     if (k == 0)
                     {
                        const double A11 = (J22 * J33) - (J23 * J32);
                        const double A12 = (J32 * J13) - (J12 * J33);
                        const double A13 = (J12 * J23) - (J22 * J13);
                        QQQ(z,y,x) = A11*u + A12*v + A13*w;

                     }

                     if (k == 1)
                     {
                        const double A21 = (J31 * J23) - (J21 * J33);
                        const double A22 = (J11 * J33) - (J13 * J31);
                        const double A23 = (J21 * J13) - (J11 * J23);
                        QQQ(z,y,x) = A21*u + A22*v + A23*w;
                     }

                     if (k == 2)
                     {
                        const double A31 = (J21 * J32) - (J31 * J22);
                        const double A32 = (J31 * J12) - (J11 * J32);
                        const double A33 = (J11 * J22) - (J12 * J21);
                        QQQ(z,y,x) = A31*u + A32*v + A33*w;
                     }

                     QQQ(z,y,x) *= W(x,y,z);
                  }
               }
               MFEM_SYNC_THREAD;
            }
            MFEM_FOREACH_THREAD(qz,x,q)
            {
               MFEM_FOREACH_THREAD(qy,y,q)
               {
                  for (int dx = 0; dx < d; ++dx) { r_u[dx] = 0.0; }
                  for (int qx = 0; qx < q; ++qx)
                  {
                     const double r_v = QQQ(qz,qy,qx);
                     for (int dx = 0; dx < d; ++dx)
                     {
                        r_u[dx] += (k == 0 ? Gt(qx,dx) : Bt(qx,dx)) * r_v;
                     }
                  }
                  for (int dx = 0; dx < d; ++dx) { QQD(qz,qy,dx) = r_u[dx]; }
               }
            }
            MFEM_SYNC_THREAD;
            MFEM_FOREACH_THREAD(qz,y,q)
            {
               MFEM_FOREACH_THREAD(dx,x,d)
               {
                  for (int dy = 0; dy < d; ++dy) { r_u[dy] = 0.0; }
                  for (int qy = 0; qy < q; ++qy)
                  {
                     const double r_v = QQD(qz,qy,dx);
                     for (int dy = 0; dy < d; ++dy)
                     {
                        r_u[dy] += (k == 1 ? Gt(qy,dy) : Bt(qy,dy)) * r_v;
                     }
                  }
                  for (int dy = 0; dy < d; ++dy) { QDD(qz,dy,dx) = r_u[dy]; }
               }
            }
            MFEM_SYNC_THREAD;
            MFEM_FOREACH_THREAD(dy,y,d)
            {
               MFEM_FOREACH_THREAD(dx,x,d)
               {
                  for (int dz = 0; dz < d; ++dz) { r_u[dz] = 0.0; }
                  for (int qz = 0; qz < q; ++qz)
                  {
                     const double r_v = QDD(qz,dy,dx);
                     for (int dz = 0; dz < d; ++dz)
                     {
                        r_u[dz] += (k == 2 ? Gt(qz,dz) : Bt(qz,dz)) * r_v;
                     }
                  }
                  for (int dz = 0; dz < d; ++dz) { Y(dx,dy,dz,c,e) += r_u[dz]; }
               }
            }
            MFEM_SYNC_THREAD;
         } // dim
      } // vdim
   });
}

static void DLFGradAssemble(const FiniteElementSpace &fes,
                            const IntegrationRule *ir,
                            const Array<int> &markers,
                            const Vector &coeff,
                            Vector &y)
{
   Mesh *mesh = fes.GetMesh();
   const int dim = mesh->Dimension();
   const FiniteElement &el = *fes.GetFE(0);
   const MemoryType mt = Device::GetDeviceMemoryType();
   const DofToQuad &maps = el.GetDofToQuad(*ir, DofToQuad::TENSOR);
   const int d = maps.ndof, q = maps.nqpt;
   constexpr int flags = GeometricFactors::JACOBIANS;
   const GeometricFactors *geom = mesh->GetGeometricFactors(*ir, flags, mt);
   decltype(&DLFGradAssemble2D<>) ker =
      dim == 2 ? DLFGradAssemble2D<> :  DLFGradAssemble3D<>;

   if (dim==2)
   {
      if (d==1 && q==1) { ker=DLFGradAssemble2D<1,1>; }
      if (d==2 && q==2) { ker=DLFGradAssemble2D<2,2>; }
      if (d==3 && q==3) { ker=DLFGradAssemble2D<3,3>; }
      if (d==4 && q==4) { ker=DLFGradAssemble2D<4,4>; }
      if (d==5 && q==5) { ker=DLFGradAssemble2D<5,5>; }
      if (d==2 && q==3) { ker=DLFGradAssemble2D<2,3>; }
      if (d==3 && q==4) { ker=DLFGradAssemble2D<3,4>; }
      if (d==4 && q==5) { ker=DLFGradAssemble2D<4,5>; }
      if (d==5 && q==6) { ker=DLFGradAssemble2D<5,6>; }
   }

   if (dim==3)
   {
      if (d==1 && q==1) { ker=DLFGradAssemble3D<1,1>; }
      if (d==2 && q==2) { ker=DLFGradAssemble3D<2,2>; }
      if (d==3 && q==3) { ker=DLFGradAssemble3D<3,3>; }
      if (d==4 && q==4) { ker=DLFGradAssemble3D<4,4>; }
      if (d==5 && q==5) { ker=DLFGradAssemble3D<5,5>; }
      if (d==2 && q==3) { ker=DLFGradAssemble3D<2,3>; }
      if (d==3 && q==4) { ker=DLFGradAssemble3D<3,4>; }
      if (d==4 && q==5) { ker=DLFGradAssemble3D<4,5>; }
      if (d==5 && q==6) { ker=DLFGradAssemble3D<5,6>; }
   }

   MFEM_VERIFY(ker, "No kernel ndof " << d << " nqpt " << q);

   const int vdim = fes.GetVDim();
   const int ne = fes.GetMesh()->GetNE();
   const int *M = markers.Read();
   const double *B = maps.B.Read();
   const double *G = maps.G.Read();
   const double *J = geom->J.Read();
   const double *W = ir->GetWeights().Read();
   double *Y = y.ReadWrite();
   ker(vdim, ne, d, q, M, B, G, J, W, coeff, Y);
}

void DomainLFGradIntegrator::AssembleDevice(const FiniteElementSpace &fes,
                                            const Array<int> &markers,
                                            Vector &b)
{

   const FiniteElement &fe = *fes.GetFE(0);
   const int qorder = 2 * fe.GetOrder();
   const Geometry::Type gtype = fe.GetGeomType();
   const IntegrationRule *ir = IntRule ? IntRule : &IntRules.Get(gtype, qorder);
   const int nq = ir->GetNPoints(), ne = fes.GetMesh()->GetNE();

   if (VectorConstantCoefficient *vcQ =
          dynamic_cast<VectorConstantCoefficient*>(&Q))
   {
      Qvec = vcQ->GetVec();
   }
   else if (VectorQuadratureFunctionCoefficient *vqfQ =
               dynamic_cast<VectorQuadratureFunctionCoefficient*>(&Q))
   {
      const QuadratureFunction &qfun = vqfQ->GetQuadFunction();
      MFEM_VERIFY(qfun.Size() == ne*nq,
                  "Incompatible QuadratureFunction dimension \n");
      MFEM_VERIFY(ir == &qfun.GetSpace()->GetElementIntRule(0),
                  "IntegrationRule used within integrator and in"
                  " QuadratureFunction appear to be different.\n");
      qfun.Read();
      Qvec.MakeRef(const_cast<QuadratureFunction&>(qfun),0);
   }
   else
   {
      const int qvdim = Q.GetVDim();
      Vector qvec(qvdim);
      Qvec.SetSize(qvdim * nq * ne);
      auto C = Reshape(Qvec.HostWrite(), qvdim, nq, ne);
      for (int e = 0; e < ne; ++e)
      {
         ElementTransformation& Tr = *fes.GetElementTransformation(e);
         for (int q = 0; q < nq; ++q)
         {
            const IntegrationPoint &ip = ir->IntPoint(q);
            Tr.SetIntPoint(&ip);
            Q.Eval(qvec, Tr, ip);
            for (int c=0; c < qvdim; ++c)
            {
               C(c,q,e) = qvec[c];
            }
         }
      }
   }
   DLFGradAssemble(fes, ir, markers, Qvec, b);
}

void VectorDomainLFGradIntegrator::AssembleDevice(const FiniteElementSpace &fes,
                                                  const Array<int> &markers,
                                                  Vector &b)
{
   const int vdim = fes.GetVDim();
   const FiniteElement &fe = *fes.GetFE(0);
   const int qorder = 2 * fe.GetOrder();
   const Geometry::Type gtype = fe.GetGeomType();
   const IntegrationRule *ir = IntRule ? IntRule : &IntRules.Get(gtype, qorder);
   const int nq = ir->GetNPoints(), ne = fes.GetMesh()->GetNE(),
             ns = fes.GetMesh()->SpaceDimension();

   if (VectorConstantCoefficient *vcQ =
          dynamic_cast<VectorConstantCoefficient*>(&Q))
   {
      Qvec = vcQ->GetVec();
   }
   else if (QuadratureFunctionCoefficient *qfQ =
               dynamic_cast<QuadratureFunctionCoefficient*>(&Q))
   {
      const QuadratureFunction &qfun = qfQ->GetQuadFunction();
      MFEM_VERIFY(qfun.Size() == ne*nq,
                  "Incompatible QuadratureFunction dimension \n");
      MFEM_VERIFY(ir == &qfun.GetSpace()->GetElementIntRule(0),
                  "IntegrationRule used within integrator and in"
                  " QuadratureFunction appear to be different.\n");
      qfun.Read();
      Qvec.MakeRef(const_cast<QuadratureFunction&>(qfun),0);
   }
   else if (VectorQuadratureFunctionCoefficient* vqfQ =
               dynamic_cast<VectorQuadratureFunctionCoefficient*>(&Q))
   {
      const QuadratureFunction &qFun = vqfQ->GetQuadFunction();
      MFEM_VERIFY(qFun.Size() == vdim * ns * nq * ne,
                  "Incompatible QuadratureFunction dimension \n");
      MFEM_VERIFY(ir == &qFun.GetSpace()->GetElementIntRule(0),
                  "IntegrationRule used within integrator and in"
                  " QuadratureFunction appear to be different");
      qFun.Read();
      Qvec.MakeRef(const_cast<QuadratureFunction &>(qFun),0);
   }
   else
   {
      Vector qvec(vdim);
      Qvec.SetSize(vdim * nq * ne);
      auto C = Reshape(Qvec.HostWrite(), vdim, nq, ne);
      for (int e = 0; e < ne; ++e)
      {
         ElementTransformation &Tr = *fes.GetElementTransformation(e);
         for (int q = 0; q < nq; ++q)
         {
            const IntegrationPoint &ip = ir->IntPoint(q);
            Tr.SetIntPoint(&ip);
            Q.Eval(qvec, Tr, ip);
            for (int c = 0; c<vdim; ++c) { C(c,q,e) = qvec[c]; }
         }
      }
   }
   DLFGradAssemble(fes, ir, markers, Qvec, b);
}

} // namespace mfem
