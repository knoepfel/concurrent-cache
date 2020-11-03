// ========================================================================
// Caching service

class CalibrationConstants {
public:
  CalibrationConstants(fhicl::ParameterSet const& pset, art::ActivityRegistry& reg)
  {
    reg.sPreProcessEvent.watch(this, &CalibrationConstants::pre_event);
  }

  double
  offset() const
  {
    ensure_loaded_entry();
    return constants_.offset();
  }

private:
  void
  pre_event(art::Event const& e, art::ScheduleContext)
  {
    current_event_no_ = e.event();
  }

  void
  ensure_loaded_entry() const
  {
    if (constants_.current_iov_supports(current_event_no_)) {
      return;
    }

    constants_.load_offset_for(current_event_no_);
  }

  std::uint32_t current_event_no_{-1u};
  ConstantsProvider mutable constants_;
};

// ========================================================================
// CalibrationConstants client

class MyModule : public art::SharedProducer {
  void
  produce(art::Event& e)
  {
    art::ServiceHandle<CalibrationConstants> calib_constants;
    auto const offset = calib_constants->offset();
  }
};
