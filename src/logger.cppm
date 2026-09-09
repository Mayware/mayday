export module logger;
export import maylog;

using namespace maylog::config;
export using namespace maylog;

export constexpr LevelInfo Db = LevelInfo("myd debug", true, CatppuccinFrappe::teal());
export constexpr LevelInfo If = LevelInfo("myd info", true, CatppuccinFrappe::flamingo(), 100);
export constexpr LevelInfo Wn = LevelInfo("myd warn", true, CatppuccinFrappe::yellow(), 200);
export constexpr LevelInfo Er = LevelInfo("myd error", true, CatppuccinFrappe::red(), 300, true);
