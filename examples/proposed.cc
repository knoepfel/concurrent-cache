// ========================================================================
// Caching service

class CalibrationConstants {
public:
  CalibrationConstants(fhicl::ParameterSet const& pset, art::ActivityRegistry& reg)
  {
    reg.sPostSubRun.watch(this, &CalibrationConstants::post_subrun);
  }

  double
  offset(art::Event const& e) const
  {
    if (auto h = cache_.entry_for(e.event())) {
      return h->offset();
    }

    auto const iov = get_iov_for(e);
    auto h = cache_.emplace(iov, get_offset_for(iov));
    return h->offset();
  }

private:
  void
  post_subrun(art::SubRun const&)
  {
    cache_.drop_unused();
  }

  cet::concurrent_cache<...> mutable cache_;
};

// ========================================================================
// CalibrationConstants client

class MyModule : public art::SharedProducer {
  void
  produce(art::Event& e)
  {
    art::ServiceHandle<CalibrationConstants> calib_constants;
    auto const offset = calib_constants->offset(e);
  }
};
