
CREATE TABLE pastes (
  paste_id SERIAL PRIMARY KEY,
  name TEXT UNIQUE NOT NULL,
  shortname VARCHAR(10) UNIQUE NOT NULL,
  content bytea NOT NULL,
  language TEXT,
  date_created TIMESTAMP WITH TIME ZONE NOT NULL
);
