/*
 * Gazette — Google News topic table
 * Copyright (c) 2026 brunocastello
 *
 * DO NOT EDIT. Regenerate with:
 *   python3 tools/generate_gnews_topics.py \
 *       --newsproxy ../NewsProxy/newsProxy.py \
 *       --out src/feeds/gazette_gnews_topics.c
 *
 * The curated topic set Newsstand 1.1 offered, carried over from NewsProxy's
 * CAAQ map. NewsProxy keys it by the base64 topic IDs the original client
 * sent, because NewsProxy answers that client; Gazette never speaks the
 * Newsstand protocol, so only what those IDs resolved to is kept.
 *
 * PORTABLE: data only, no system headers.
 */

#include "feeds/gazette_googlenews.h"

/* Group names, in the order Newsstand presented them. */
const char *const kGazetteTopicGroups[] = {
    "Technology",
    "Sports",
    "Interests",
    "Business",
    "Entertainment",
    "Science",
    "Gaming",
    "Health",
    0
};

const int kGazetteTopicGroupCount = 8;

const GazetteTopic kGazetteTopics[] = {

    /* Technology */
    { 0, "Technology", kGazetteTopicSection, "TECHNOLOGY" },
    { 0, "Amazon technology", kGazetteTopicSearch, "Amazon technology" },
    { 0, "Apple Inc", kGazetteTopicSearch, "Apple Inc" },
    { 0, "Apple Watch", kGazetteTopicSearch, "Apple Watch" },
    { 0, "Artificial Intelligence", kGazetteTopicSearch, "Artificial Intelligence" },
    { 0, "Cameras photography", kGazetteTopicSearch, "cameras photography" },
    { 0, "Cybersecurity", kGazetteTopicSearch, "cybersecurity" },
    { 0, "Digital privacy", kGazetteTopicSearch, "digital privacy" },
    { 0, "Drones", kGazetteTopicSearch, "drones" },
    { 0, "Consumer electronics", kGazetteTopicSearch, "consumer electronics" },
    { 0, "Google technology", kGazetteTopicSearch, "Google technology" },
    { 0, "IPad Apple", kGazetteTopicSearch, "iPad Apple" },
    { 0, "IPhone Apple", kGazetteTopicSearch, "iPhone Apple" },
    { 0, "Mac Apple", kGazetteTopicSearch, "Mac Apple" },
    { 0, "MacOS Apple", kGazetteTopicSearch, "macOS Apple" },
    { 0, "Microsoft", kGazetteTopicSearch, "Microsoft" },
    { 0, "Computers PC", kGazetteTopicSearch, "computers PC" },
    { 0, "Samsung Galaxy", kGazetteTopicSearch, "Samsung Galaxy" },
    { 0, "Smartphones", kGazetteTopicSearch, "smartphones" },
    { 0, "Smartwatches wearables", kGazetteTopicSearch, "smartwatches wearables" },
    { 0, "Virtual reality VR", kGazetteTopicSearch, "virtual reality VR" },

    /* Sports */
    { 1, "Sports", kGazetteTopicSection, "SPORTS" },
    { 1, "Baseball MLB", kGazetteTopicSearch, "baseball MLB" },
    { 1, "Basketball NBA", kGazetteTopicSearch, "basketball NBA" },
    { 1, "Cricket", kGazetteTopicSearch, "cricket" },
    { 1, "Fantasy football", kGazetteTopicSearch, "fantasy football" },
    { 1, "Fantasy sports", kGazetteTopicSearch, "fantasy sports" },
    { 1, "NFL football", kGazetteTopicSearch, "NFL football" },
    { 1, "Golf PGA", kGazetteTopicSearch, "golf PGA" },
    { 1, "Hockey NHL", kGazetteTopicSearch, "hockey NHL" },
    { 1, "Lacrosse", kGazetteTopicSearch, "lacrosse" },
    { 1, "NCAA basketball", kGazetteTopicSearch, "NCAA basketball" },
    { 1, "NCAA football", kGazetteTopicSearch, "NCAA football" },
    { 1, "Rugby", kGazetteTopicSearch, "rugby" },
    { 1, "Skateboarding", kGazetteTopicSearch, "skateboarding" },
    { 1, "Skiing", kGazetteTopicSearch, "skiing" },
    { 1, "Snowboarding", kGazetteTopicSearch, "snowboarding" },
    { 1, "Soccer football", kGazetteTopicSearch, "soccer football" },
    { 1, "Surfing", kGazetteTopicSearch, "surfing" },
    { 1, "Table tennis", kGazetteTopicSearch, "table tennis" },
    { 1, "Tennis", kGazetteTopicSearch, "tennis" },
    { 1, "Volleyball", kGazetteTopicSearch, "volleyball" },

    /* Interests */
    { 2, "3D printing", kGazetteTopicSearch, "3D printing" },
    { 2, "Animals wildlife", kGazetteTopicSearch, "animals wildlife" },
    { 2, "Art news", kGazetteTopicSearch, "art news" },
    { 2, "Astronomy", kGazetteTopicSearch, "astronomy" },
    { 2, "Backpacking travel", kGazetteTopicSearch, "backpacking travel" },
    { 2, "Baking", kGazetteTopicSearch, "baking" },
    { 2, "BASE jumping", kGazetteTopicSearch, "BASE jumping" },
    { 2, "Beauty cosmetics", kGazetteTopicSearch, "beauty cosmetics" },
    { 2, "Beekeeping", kGazetteTopicSearch, "beekeeping" },
    { 2, "Beer craft brewing", kGazetteTopicSearch, "beer craft brewing" },
    { 2, "Bird watching", kGazetteTopicSearch, "bird watching" },
    { 2, "Camping outdoors", kGazetteTopicSearch, "camping outdoors" },
    { 2, "Cars automotive", kGazetteTopicSearch, "cars automotive" },
    { 2, "Cats pets", kGazetteTopicSearch, "cats pets" },
    { 2, "Chess", kGazetteTopicSearch, "chess" },
    { 2, "Coffee", kGazetteTopicSearch, "coffee" },
    { 2, "Comics", kGazetteTopicSearch, "comics" },
    { 2, "Computer programming", kGazetteTopicSearch, "computer programming" },
    { 2, "Cooking recipes", kGazetteTopicSearch, "cooking recipes" },
    { 2, "Cycling biking", kGazetteTopicSearch, "cycling biking" },
    { 2, "Design", kGazetteTopicSearch, "design" },
    { 2, "DIY home improvement", kGazetteTopicSearch, "DIY home improvement" },
    { 2, "Dogs pets", kGazetteTopicSearch, "dogs pets" },
    { 2, "Drawing illustration", kGazetteTopicSearch, "drawing illustration" },
    { 2, "Horse riding equestrian", kGazetteTopicSearch, "horse riding equestrian" },
    { 2, "Fishing", kGazetteTopicSearch, "fishing" },
    { 2, "Formula 1", kGazetteTopicSearch, "Formula 1" },
    { 2, "Fossil collecting", kGazetteTopicSearch, "fossil collecting" },
    { 2, "Gardening plants", kGazetteTopicSearch, "gardening plants" },
    { 2, "Geocaching", kGazetteTopicSearch, "geocaching" },
    { 2, "Ghost hunting paranormal", kGazetteTopicSearch, "ghost hunting paranormal" },
    { 2, "Gymnastics", kGazetteTopicSearch, "gymnastics" },
    { 2, "Hiking trails", kGazetteTopicSearch, "hiking trails" },
    { 2, "Horoscopes astrology", kGazetteTopicSearch, "horoscopes astrology" },
    { 2, "Hunting", kGazetteTopicSearch, "hunting" },
    { 2, "Interior design", kGazetteTopicSearch, "interior design" },
    { 2, "Jigsaw puzzles", kGazetteTopicSearch, "jigsaw puzzles" },
    { 2, "Knitting", kGazetteTopicSearch, "knitting" },
    { 2, "Lego", kGazetteTopicSearch, "Lego" },
    { 2, "Martial arts MMA", kGazetteTopicSearch, "martial arts MMA" },
    { 2, "Military defense", kGazetteTopicSearch, "military defense" },
    { 2, "Motorcycles", kGazetteTopicSearch, "motorcycles" },
    { 2, "Mountain climbing", kGazetteTopicSearch, "mountain climbing" },
    { 2, "National parks", kGazetteTopicSearch, "national parks" },
    { 2, "Painting art", kGazetteTopicSearch, "painting art" },
    { 2, "Papermaking craft", kGazetteTopicSearch, "papermaking craft" },
    { 2, "Photography", kGazetteTopicSearch, "photography" },
    { 2, "Plants gardening", kGazetteTopicSearch, "plants gardening" },
    { 2, "Politics", kGazetteTopicSearch, "politics" },
    { 2, "US Senate", kGazetteTopicSearch, "US Senate" },
    { 2, "US House Representatives", kGazetteTopicSearch, "US House Representatives" },
    { 2, "US Supreme Court", kGazetteTopicSearch, "US Supreme Court" },
    { 2, "Pottery ceramics", kGazetteTopicSearch, "pottery ceramics" },
    { 2, "Quilting", kGazetteTopicSearch, "quilting" },
    { 2, "Robotics", kGazetteTopicSearch, "robotics" },
    { 2, "Sailing", kGazetteTopicSearch, "sailing" },
    { 2, "Scrapbooking", kGazetteTopicSearch, "scrapbooking" },
    { 2, "Sculpture art", kGazetteTopicSearch, "sculpture art" },
    { 2, "Sewing fashion", kGazetteTopicSearch, "sewing fashion" },
    { 2, "Social media", kGazetteTopicSearch, "social media" },
    { 2, "Speedcubing Rubik", kGazetteTopicSearch, "speedcubing Rubik" },
    { 2, "Scuba diving", kGazetteTopicSearch, "scuba diving" },
    { 2, "Urban exploration", kGazetteTopicSearch, "urban exploration" },
    { 2, "Video editing", kGazetteTopicSearch, "video editing" },
    { 2, "Visual design", kGazetteTopicSearch, "visual design" },
    { 2, "Whale watching", kGazetteTopicSearch, "whale watching" },
    { 2, "Wine", kGazetteTopicSearch, "wine" },
    { 2, "Writing authors", kGazetteTopicSearch, "writing authors" },

    /* Business */
    { 3, "Business", kGazetteTopicSection, "BUSINESS" },
    { 3, "Cryptocurrency Bitcoin", kGazetteTopicSearch, "cryptocurrency Bitcoin" },
    { 3, "E-commerce", kGazetteTopicSearch, "e-commerce" },
    { 3, "Economy", kGazetteTopicSearch, "economy" },
    { 3, "Entrepreneurship startups", kGazetteTopicSearch, "entrepreneurship startups" },
    { 3, "Investing stocks finance", kGazetteTopicSearch, "investing stocks finance" },
    { 3, "Leadership business", kGazetteTopicSearch, "leadership business" },
    { 3, "Marketing advertising", kGazetteTopicSearch, "marketing advertising" },
    { 3, "Personal finance money", kGazetteTopicSearch, "personal finance money" },
    { 3, "Retirement", kGazetteTopicSearch, "retirement" },
    { 3, "Small business", kGazetteTopicSearch, "small business" },
    { 3, "Business strategy", kGazetteTopicSearch, "business strategy" },
    { 3, "Taxes IRS", kGazetteTopicSearch, "taxes IRS" },

    /* Entertainment */
    { 4, "Entertainment", kGazetteTopicSection, "ENTERTAINMENT" },
    { 4, "Books literature", kGazetteTopicSearch, "books literature" },
    { 4, "Celebrities Hollywood", kGazetteTopicSearch, "celebrities Hollywood" },
    { 4, "Classical music", kGazetteTopicSearch, "classical music" },
    { 4, "Country music", kGazetteTopicSearch, "country music" },
    { 4, "Disney", kGazetteTopicSearch, "Disney" },
    { 4, "Documentaries film", kGazetteTopicSearch, "documentaries film" },
    { 4, "EDM electronic music", kGazetteTopicSearch, "EDM electronic music" },
    { 4, "Film industry Hollywood", kGazetteTopicSearch, "film industry Hollywood" },
    { 4, "Movie reviews", kGazetteTopicSearch, "movie reviews" },
    { 4, "Movies cinema", kGazetteTopicSearch, "movies cinema" },
    { 4, "Music", kGazetteTopicSearch, "music" },
    { 4, "Music festivals", kGazetteTopicSearch, "music festivals" },
    { 4, "Music streaming Spotify", kGazetteTopicSearch, "music streaming Spotify" },
    { 4, "Netflix streaming", kGazetteTopicSearch, "Netflix streaming" },
    { 4, "Podcasts", kGazetteTopicSearch, "podcasts" },
    { 4, "Pop music", kGazetteTopicSearch, "pop music" },
    { 4, "Reality TV", kGazetteTopicSearch, "reality TV" },
    { 4, "Rock music", kGazetteTopicSearch, "rock music" },
    { 4, "Star Trek", kGazetteTopicSearch, "Star Trek" },
    { 4, "Star Wars", kGazetteTopicSearch, "Star Wars" },
    { 4, "Hip-hop rap music", kGazetteTopicSearch, "hip-hop rap music" },
    { 4, "Hulu streaming", kGazetteTopicSearch, "Hulu streaming" },
    { 4, "Indie films", kGazetteTopicSearch, "indie films" },
    { 4, "K-Pop music", kGazetteTopicSearch, "K-Pop music" },
    { 4, "Marvel movies", kGazetteTopicSearch, "Marvel movies" },
    { 4, "Media entertainment news", kGazetteTopicSearch, "media entertainment news" },
    { 4, "Fashion style", kGazetteTopicSearch, "fashion style" },
    { 4, "Theatre Broadway", kGazetteTopicSearch, "theatre Broadway" },
    { 4, "Theatre reviews", kGazetteTopicSearch, "theatre reviews" },
    { 4, "TV shows television", kGazetteTopicSearch, "TV shows television" },
    { 4, "YouTube creators", kGazetteTopicSearch, "YouTube creators" },

    /* Science */
    { 5, "Science", kGazetteTopicSection, "SCIENCE" },
    { 5, "Biology", kGazetteTopicSearch, "biology" },
    { 5, "Computer science", kGazetteTopicSearch, "computer science" },
    { 5, "Physics", kGazetteTopicSearch, "physics" },
    { 5, "Geology", kGazetteTopicSearch, "geology" },
    { 5, "Genetics DNA", kGazetteTopicSearch, "genetics DNA" },
    { 5, "Environment climate change", kGazetteTopicSearch, "environment climate change" },
    { 5, "NASA space", kGazetteTopicSearch, "NASA space" },
    { 5, "Neuroscience brain", kGazetteTopicSearch, "neuroscience brain" },
    { 5, "Space astronomy", kGazetteTopicSearch, "space astronomy" },
    { 5, "SpaceX", kGazetteTopicSearch, "SpaceX" },
    { 5, "Wildlife nature", kGazetteTopicSearch, "wildlife nature" },

    /* Gaming */
    { 6, "Video games gaming", kGazetteTopicSearch, "video games gaming" },
    { 6, "2K Games", kGazetteTopicSearch, "2K Games" },
    { 6, "Bethesda games", kGazetteTopicSearch, "Bethesda games" },
    { 6, "Nintendo", kGazetteTopicSearch, "Nintendo" },
    { 6, "PlayStation Sony", kGazetteTopicSearch, "PlayStation Sony" },
    { 6, "Rockstar Games GTA", kGazetteTopicSearch, "Rockstar Games GTA" },
    { 6, "Square Enix games", kGazetteTopicSearch, "Square Enix games" },
    { 6, "Ubisoft games", kGazetteTopicSearch, "Ubisoft games" },
    { 6, "Xbox Microsoft gaming", kGazetteTopicSearch, "Xbox Microsoft gaming" },

    /* Health */
    { 7, "Health", kGazetteTopicSection, "HEALTH" },
    { 7, "CrossFit", kGazetteTopicSearch, "CrossFit" },
    { 7, "Exercise fitness", kGazetteTopicSearch, "exercise fitness" },
    { 7, "Keto diet", kGazetteTopicSearch, "keto diet" },
    { 7, "Meditation mindfulness", kGazetteTopicSearch, "meditation mindfulness" },
    { 7, "Running marathon", kGazetteTopicSearch, "running marathon" },
    { 7, "Veganism", kGazetteTopicSearch, "veganism" },
    { 7, "Weightlifting", kGazetteTopicSearch, "weightlifting" },
};

const int kGazetteTopicCount =
    (int)(sizeof kGazetteTopics / sizeof kGazetteTopics[0]);
