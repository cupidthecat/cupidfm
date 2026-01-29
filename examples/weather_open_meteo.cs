// weather_open_meteo.cs - HTTPS weather popup using Open-Meteo
//
// Suggested bind:
//   F6 - open weather panel
//
// Install:
//   cp plugins/examples/weather_open_meteo.cs ~/.cupidfm/plugins/
//
// This plugin uses https://api.open-meteo.com (HTTPS) via CupidScript http_get().

fn on_load() {
  fm.notify("weather_open_meteo loaded (F6)");
  fm.console_print("weather_open_meteo: loaded (F6)");
  fm.bind("F6", "weather_panel");
}

fn weather_panel(key) {
  let lat = get_or_prompt("weather.lat", "Latitude", "52.52");
  if (lat == nil || lat == "") {
    return true;
  }
  let lon = get_or_prompt("weather.lon", "Longitude", "13.41");
  if (lon == nil || lon == "") {
    return true;
  }

  let url = build_open_meteo_url(lat, lon);

  fm.ui_status_set("Weather: fetching... (HTTPS)");
  let resp = http_get(url);
  fm.ui_status_clear();

  if (resp == nil) {
    fm.popup("Weather", "Request failed");
    return true;
  }

  let status = resp["status"];
  if (status != 200) {
    let body = resp["body"];
    fm.popup("Weather", "HTTP " + fmt("%v", status) + "\n" + fmt("%v", body));
    return true;
  }

  let data = json_parse(resp["body"]);
  if (data == nil) {
    fm.popup("Weather", "Invalid JSON response");
    return true;
  }

  let msg = build_weather_message(lat, lon, data);
  fm.popup("Weather", msg);
  return true;
}

fn get_or_prompt(cache_key, title, def_val) {
  let v = fm.cache_get(cache_key);
  if (v == nil || v == "") {
    v = fm.prompt(title, def_val);
    if (v != nil && v != "") {
      fm.cache_set(cache_key, v);
    }
  }
  return v;
}

fn build_open_meteo_url(lat, lon) {
  return "https://api.open-meteo.com/v1/forecast?latitude=" + lat +
         "&longitude=" + lon +
         "&current=temperature_2m,wind_speed_10m" +
         "&hourly=temperature_2m,relative_humidity_2m,wind_speed_10m";
}

fn build_weather_message(lat, lon, data) {
  let lines = list();
  push(lines, "Location: " + lat + ", " + lon);

  let current = data["current"];
  if (current != nil) {
    let time = current["time"];
    let temp = current["temperature_2m"];
    let wind = current["wind_speed_10m"];
    let line = "Current: " + fmt("%v", temp) + "°C" +
               ", wind " + fmt("%v", wind) + " m/s";
    if (time != nil) {
      line = line + " @ " + fmt("%v", time);
    }
    push(lines, line);
  } else {
    push(lines, "Current: n/a");
  }

  let hourly = data["hourly"];
  if (hourly != nil) {
    let times = hourly["time"];
    let temps = hourly["temperature_2m"];
    let hums = hourly["relative_humidity_2m"];
    let winds = hourly["wind_speed_10m"];

    push(lines, "Next hours:");
    let i = 0;
    let shown = 0;
    let total = safe_len(times);
    while (i < total && shown < 4) {
      let t = safe_list_at(times, i);
      let temp = safe_list_at(temps, i);
      let hum = safe_list_at(hums, i);
      let wind = safe_list_at(winds, i);

      if (t != nil) {
        let row = "  " + fmt("%v", t) + ": " +
                  fmt("%v", temp) + "°C" +
                  ", RH " + fmt("%v", hum) + "%" +
                  ", wind " + fmt("%v", wind) + " m/s";
        push(lines, row);
        shown = shown + 1;
      }
      i = i + 1;
    }
  }

  return join_lines(lines);
}

fn safe_len(v) {
  if (v == nil) {
    return 0;
  }
  return len(v);
}

fn safe_list_at(v, idx) {
  if (v == nil) {
    return nil;
  }
  if (idx < 0) {
    return nil;
  }
  if (idx >= len(v)) {
    return nil;
  }
  return v[idx];
}

fn join_lines(lines) {
  let out = "";
  let i = 0;
  let n = len(lines);
  while (i < n) {
    if (i > 0) {
      out = out + "\n";
    }
    out = out + lines[i];
    i = i + 1;
  }
  return out;
}
