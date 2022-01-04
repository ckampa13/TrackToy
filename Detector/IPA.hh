//
//  Inner proton absorber.  Currently a thin cylinder
//
#ifndef TrackToy_Detector_IPA_hh
#define TrackToy_Detector_IPA_hh
#include "KinKal/MatEnv/MatDBInfo.hh"
#include "KinKal/MatEnv/DetMaterial.hh"
#include "KinKal/General/TimeRange.hh"
#include "TrackToy/General/TrajUtilities.hh"
#include "TrackToy/Detector/CylindricalShell.hh"
namespace TrackToy {
  class IPA {
    public:
      enum IPAType { unknown=-1, cylindrical=1, propeller };
      IPA(MatEnv::MatDBInfo const& matdbinfo,std::string const& file);
      auto const& cylinder() const { return cyl_; }
      auto const& material() const { return *mat_; }
      auto type() const { return type_; }
      // extend a trajectory through the IPA.  Return value specifies if the particle continues downsream (true) or stops in the target or exits the field upstream (false)
      template<class PKTRAJ> bool extendTrajectory(KinKal::BFieldMap const& bfield, PKTRAJ& pktraj,TimeRanges& intersections) const;
      void print(std::ostream& os) const;
    private:
      IPAType type_;
      CylindricalShell cyl_;
      const MatEnv::DetMaterial* mat_;
  };


  template<class PKTRAJ> bool IPA::extendTrajectory(KinKal::BFieldMap const& bfield, PKTRAJ& pktraj, TimeRanges& intersections) const {
    bool retval(false);
    intersections.clear();
    // compute the time tolerance based on the speed.
    double ttol = 3.0/pktraj.speed(pktraj.range().begin());
    // record the end of the previous extension; this is where new extensions start
    double tstart = pktraj.back().range().begin();
    double energy = pktraj.energy(tstart);
    // extend through the IPA or exiting the BField (backwards)
    retval = extendZ(pktraj,bfield, cyl_.zmax(), ttol);
//   std::cout << "IPA extend " << retval << std::endl;
    if(retval){
//      auto pstart = pktraj.position3(tstart);
//      std::cout << "zstart " << pstart.Z() << " Z extend " << zipa << std::endl;
      // first find the intersections.
      static double tstep(0.01);
      cyl_.intersect(pktraj,intersections,tstart,tstep);
      if(intersections.size() > 0){
        for (auto const& ipainter : intersections) {
          double mom = pktraj.momentum(ipainter.mid());
          double plen = pktraj.speed(ipainter.mid())*ipainter.range();
          double de = mat_->energyLossMPV(mom,plen,pktraj.mass()); // should sample TODO
          energy += de;
        }
        retval = updateEnergy(pktraj,intersections.back().end(),energy);
      }
    }
    return retval;
  }
}
#endif
