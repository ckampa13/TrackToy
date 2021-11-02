//
// Cylindrical tracker
//
#ifndef TrackToy_Detector_Tracker_hh
#define TrackToy_Detector_Tracker_hh
#include "TrackToy/Detector/HollowCylinder.hh"
#include "KinKal/MatEnv/MatDBInfo.hh"
#include "KinKal/General/Vectors.hh"
#include "KinKal/Trajectory/Line.hh"
#include "KinKal/Detector/StrawXing.hh"
#include "KinKal/Detector/StrawMaterial.hh"
#include "KinKal/Examples/SimpleWireHit.hh"
#include "KinKal/Examples/ScintHit.hh" // add scint hit TODO
#include "TRandom3.h"
#include <string>
#include <vector>
#include <iostream>
namespace TrackToy {
  class Tracker {
    public:
      enum CellOrientation{azimuthal=0,axial};
      Tracker(MatEnv::MatDBInfo const& matdbinfo,std::string const& tfile);
      auto const& cylinder() const { return cyl_; }
      unsigned nCells() const { return ncells_; }
      double density() const { return density_;} // gm/mm^3
      double driftVelocity() const { return vdrift_; } // mm/ns
      double propagationVelocity() const { return vprop_; } // mm/ns
      double driftResolution() const { return sigt_; } // ns
      double propagationResolution() const { return sigl_; } // ns
      double cellDensity() const { return cellDensity_;} // N/mm
      double cellRadius() const { return smat_->strawRadius(); }
      double rMin() const { return cyl_.rmin(); }
      double rMax() const { return cyl_.rmax(); }
      double zMin() const { return cyl_.zmin(); }
      double zMax() const { return cyl_.zmax(); }
      void print(std::ostream& os) const;
      const KinKal::StrawMaterial* strawMaterial() const { return smat_; }
      // simulate hit and straw crossings along a particle trajectory.  This also updates the trajectory for BField and material effects
      template<class KTRAJ> bool simulateHits(KinKal::BFieldMap const& bfield,
          KinKal::ParticleTrajectory<KTRAJ>& pktraj,
          std::vector<std::shared_ptr<KinKal::Hit<KTRAJ>>>& hits,
          std::vector<std::shared_ptr<KinKal::ElementXing<KTRAJ>>>& xings,
          std::vector<KinKal::TimeRange>& tinters,
          std::vector<double>& htimes) const;
    private:
      // helper functions for simulating hits
      // create a line representing the wire for a time on a particle trajector.  This embeds the timing information
      template <class KTRAJ> KinKal::Line wireLine(KinKal::ParticleTrajectory<KTRAJ> const& pktraj, double htime) const;
      // simulate hit and xing for a particular time on a particle trajectory and add them to the lists
      template <class KTRAJ> void simulateHit(KinKal::BFieldMap const& bfield,
          KinKal::ParticleTrajectory<KTRAJ> const& pktraj,
          double htime,
          std::vector<std::shared_ptr<KinKal::Hit<KTRAJ>>>& hits,
          std::vector<std::shared_ptr<KinKal::ElementXing<KTRAJ>>>& xings ) const;
      // udpdate the trajectory for material effects
      template <class KTRAJ> void updateTraj(KinKal::BFieldMap const& bfield,
          KinKal::ParticleTrajectory<KTRAJ>& pktraj, const KinKal::ElementXing<KTRAJ>* sxing) const;
    private:
      HollowCylinder cyl_; // geometric form of the tracker
      CellOrientation orientation_; // orientation of the cells
      unsigned ncells_; // number of cells
      double cellDensity_; // effective linear cell density mm^-1
      double density_; // total average density gm/mm^3
      KinKal::StrawMaterial* smat_; // straw material
      double vdrift_; // drift velocity
      double vprop_; // signal propagation velocity
      double sigt_; // transverse measurement time resolution sigma
      double sigl_; // longitudinal measurement time resolution sigma
      mutable TRandom3 tr_; // random number generator
  };

  template<class KTRAJ> bool Tracker::simulateHits(KinKal::BFieldMap const& bfield,
      KinKal::ParticleTrajectory<KTRAJ>& pktraj,
      std::vector<std::shared_ptr<KinKal::Hit<KTRAJ>>>& hits,
      std::vector<std::shared_ptr<KinKal::ElementXing<KTRAJ>>>& xings,
      std::vector<KinKal::TimeRange>& tinters, std::vector<double>& htimes) const {
    double tstart = pktraj.back().range().begin();
    double speed = pktraj.speed(tstart);
    double tol = 1.0/speed; // 1mm accuracy
    double tstep = cellRadius()/speed;
    // extend through the tracker to get the ranges
    extendZ(pktraj,bfield, bfield.zMin(), cylinder().zmax(), tol);
    // find intersections with tracker
    cylinder().intersect(pktraj,tinters,tstart,tstep);
//    std::cout << "ninters " << tinters.size() << std::endl;
    for(auto const& tinter : tinters) {
      double clen(0.0);
      double time = tinter.begin();
      while(time < tinter.end()){
        auto vel = pktraj.velocity(time);
        if(orientation_ == azimuthal){
          auto pos = pktraj.position3(time);
          auto rdir = KinKal::VEC3(pos.X(),pos.Y(),0.0).Unit(); // radial direction
          double vr = vel.Dot(rdir); // radial component of velocity
          double vtot = sqrt(vel.Z()*vel.Z() + vr*vr);
          clen += vtot*tstep;
        } else {
          clen += vel.R()*tstep;
        }
        time += tstep;
      }
      unsigned ncells = (unsigned)rint(clen*cellDensity_);
      double hstep = tinter.range()/(ncells+1);
      double htime = tinter.begin()+0.5*tstep;
      for(unsigned icell=0;icell<ncells;++icell){
        htimes.push_back(htime);
        // extend the trajectory to this time
        extendTraj(bfield,pktraj,htime,tol);
        // create hits and xings for this time
        simulateHit(bfield,pktraj,htime,hits,xings);
        // update the trajector for the effect of this material
        updateTraj(bfield, pktraj,xings.back().get());
        // update to the next
        htime += hstep;
      }
    }
    return true;
  }

  template <class KTRAJ> void Tracker::simulateHit(KinKal::BFieldMap const& bfield, KinKal::ParticleTrajectory<KTRAJ> const& pktraj,
      double htime,
      std::vector<std::shared_ptr<KinKal::Hit<KTRAJ>>>& hits,
      std::vector<std::shared_ptr<KinKal::ElementXing<KTRAJ>>>& xings ) const {
    using PTCA = KinKal::PiecewiseClosestApproach<KTRAJ,KinKal::Line>;
    using WIREHIT = KinKal::SimpleWireHit<KTRAJ>;
    using STRAWXING = KinKal::StrawXing<KTRAJ>;
    using STRAWXINGPTR = std::shared_ptr<STRAWXING>;
    // create the line representing this hit's wire.  The line embeds the timing information
    KinKal::Line const& wline = wireLine(pktraj,htime);
    // find the POCA between the particle trajectory and the wire line
    KinKal::CAHint tphint(htime,htime);
    static double tprec(1e-8); // TPOCA precision
    static double ambigdoca(0.25); // minimum distance to use drift
    PTCA tp(pktraj,wline,tphint,tprec);
    // define the initial ambiguity; it is the MC true value by default
    KinKal::WireHitState::LRAmbig ambig(KinKal::WireHitState::null);
    if(fabs(tp.doca())> ambigdoca) ambig = tp.doca() < 0 ? KinKal::WireHitState::left : KinKal::WireHitState::right;
    KinKal::WireHitState::Dimension dim(KinKal::WireHitState::time);
    double nullvar = (cellRadius()*cellRadius())/3.0;
    KinKal::WireHitState whstate(ambig, dim, nullvar, 0.0);
    // create the hit
    hits.push_back(std::make_shared<WIREHIT>(bfield, tp, whstate, vdrift_, sigt_*sigt_, cellRadius()));
    // create the straw xing
    auto xing = std::make_shared<STRAWXING>(tp,*smat_);
    xings.push_back(xing);
  }

  template <class KTRAJ> KinKal::Line Tracker::wireLine(KinKal::ParticleTrajectory<KTRAJ> const& pktraj, double htime) const {
  // find the position and direction of the particle at this time
    auto pos = pktraj.position3(htime);
    // define the drift and wire directions
    KinKal::VEC3 ddir, wdir;
    auto eta = tr_.Uniform(-M_PI,M_PI);
    if(orientation_ == azimuthal){
      auto rdir = KinKal::VEC3(pos.X(),pos.Y(),0.0).Unit(); // radial direction
      auto fdir = KinKal::VEC3(pos.Y(),-pos.X(),0.0).Unit(); // azimuthal direction
      float phimax = atanf(pos.Rho()/cyl_.rmin());
      double phi = tr_.Uniform(-phimax,phimax);
      wdir = fdir*cos(phi) + rdir*sin(phi);
    // generate a random drift perp to the hit: this defines the wire position
      ddir = cos(eta)*KinKal::VEC3(wdir.Y(),-wdir.X(),0.0) + KinKal::VEC3(0.0,0.0,sin(eta));
    } else {
      wdir = KinKal::VEC3(0.0,0.0,1.0);
      ddir = KinKal::VEC3(cos(eta),sin(eta),0.0);
    }
    // uniform drift distance = uniform impact parameter
    double rdrift = tr_.Uniform(0.0,cellRadius());
    auto wpos = pos + rdrift*ddir;
    // find the wire ends; this is where the wire crosses the outer envelope
    auto mpos = wpos;
    double dprop, wlen;
    if(orientation_ == azimuthal){
      double rwire = wpos.Rho(); // radius of the wire position
      // adjust if necessary
      if(rwire > rMax()){
        auto rdir = KinKal::VEC3(pos.X(),pos.Y(),0.0).Unit(); // radial direction
        wpos -= rdir*(rwire - rMax() + smat_->strawRadius());
        rwire = wpos.Rho();
      }
      // find crossing of outer cylinder
      double wdot = wpos.Dot(wdir);
      double term = sqrt(0.25*wdot*wdot + (rMax()*rMax() - rwire*rwire));
      double d1 = wdot+term;
      double d2 = wdot-term;
      wlen = fabs(d1)+fabs(d2);
      // choose the shortest propagation distance to define the measurement (earliest time)
      if(fabs(d1) < fabs(d2)){
        mpos += d1*wdir;
        dprop = fabs(d1);
      } else {
        mpos += d2*wdir;
        dprop = fabs(d2);
      }
    } else {
      // wire ends are at zmin and zmax
      double zmax = zMax() - wpos.Z();
      double zmin = zMin() - wpos.Z();
      wlen = zmax - zmin;
      if(fabs(zmax) < fabs(zmin)){
        mpos += zmax*wdir;
        dprop = fabs(zmax);
      } else {
        mpos += zmin*wdir;
        dprop = fabs(zmin);
      }
    }
    // need to check the propagation direction sign TODO
    // measurement time includes propagation and drift
    double mtime = htime + dprop/vprop_ + rdrift/vdrift_;
    // smear measurement time by the resolution
    mtime = tr_.Gaus(mtime,sigt_);
    // construct the trajectory for this hit.  Note this embeds the timing and propagation information
    return KinKal::Line(mpos,mtime,wdir*vprop_,wlen);
  }

  template <class KTRAJ> void Tracker::updateTraj(KinKal::BFieldMap const& bfield,
      KinKal::ParticleTrajectory<KTRAJ>& pktraj, const KinKal::ElementXing<KTRAJ>* sxing) const {
  // simulate energy loss and multiple scattering from this xing
    double txing = sxing->crossingTime();
    auto const& endpiece = pktraj.nearestPiece(txing);
    double mom = endpiece.momentum(txing);
    auto endmom = endpiece.momentum4(txing);
    auto endpos = endpiece.position4(txing);
    std::array<double,3> dmom {0.0,0.0,0.0}, momvar {0.0,0.0,0.0};
    sxing->materialEffects(pktraj,KinKal::TimeDir::forwards, dmom, momvar);
    for(int idir=0;idir<=KinKal::MomBasis::phidir_; idir++) {
      auto mdir = static_cast<KinKal::MomBasis::Direction>(idir);
      double momsig = sqrt(momvar[idir]);
      double dm;
      // generate a random effect given this variance and mean.  Note momEffect is scaled to momentum
      switch( mdir ) {
        case KinKal::MomBasis::perpdir_: case KinKal::MomBasis::phidir_ :
          dm = tr_.Gaus(dmom[idir],momsig);
          break;
        case KinKal::MomBasis::momdir_ :
          dm = std::min(0.0,tr_.Gaus(dmom[idir],momsig));
          break;
        default:
          throw std::invalid_argument("Invalid direction");
      }
      auto dmvec = endpiece.direction(txing,mdir);
      dmvec *= dm*mom;
      endmom.SetCoordinates(endmom.Px()+dmvec.X(), endmom.Py()+dmvec.Y(), endmom.Pz()+dmvec.Z(),endmom.M());
    }
    // generate a new piece and append
    KinKal::VEC3 bnom = bfield.fieldVect(endpos.Vect());
    KTRAJ newend(endpos,endmom,endpiece.charge(),bnom,KinKal::TimeRange(txing,pktraj.range().end()));
    //      newend.print(cout,1);
    pktraj.append(newend,true); // allow truncation if needed
  }

}
#endif

