void bind_lobby(){
 dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("online-capacity"))->SetValue("4");
 listen(menu,"open-online","click",[&](auto&){visible(menu,"menu-card",false);visible(menu,"online-card",true);});
 listen(menu,"online-back","click",[&](auto&){show_menu();});
 const auto connect=[&](bool host,bool resume=false){
  const auto value=[&](const char*id){return dynamic_cast<Rml::ElementFormControl*>(menu->GetElementById(id))->GetValue();};
  UiAction action;action.type=host?UiActionType::HostGame:UiActionType::JoinGame;
  action.value=resume?1:0;
  action.text=nlohmann::json{{"name",value("online-name")},{"address",value("online-address")},{"password",value("online-password")},{"capacity",value("online-capacity")}}.dump();
  dynamic_cast<Rml::ElementFormControl*>(menu->GetElementById("online-password"))->SetValue("");queue(std::move(action));
 };
 listen(menu,"online-host","click",[&,connect](auto&){connect(true);});
 listen(menu,"online-resume","click",[&,connect](auto&){connect(true,true);});
 listen(menu,"online-join","click",[&,connect](auto&){connect(false);});
 listen(menu,"lobby-team","change",[&](auto&){
  if(syncing_controls)return;
  auto value=dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("lobby-team"))->GetValue();
  UiAction a;a.type=UiActionType::LobbyCommand;a.value=int(LobbyAction::Team);a.quantity=std::stoi(value);queue(a);
 });
 listen(menu,"lobby-ready","click",[&](auto&){
  bool ready=false;for(const auto&p:lobby_state.players)if(p.id==lobby_player)ready=p.ready;
  UiAction a;a.type=UiActionType::LobbyCommand;a.value=int(LobbyAction::Ready);a.quantity=ready?0:1;queue(a);
 });
 for(int slot=0;slot<6;++slot){
  listen(menu,("lobby-slot-action-"+std::to_string(slot)).c_str(),"click",[&,slot](auto&){
   if(lobby_state.host!=lobby_player)return;
   UiAction a;a.type=UiActionType::LobbyCommand;
   if(slot<int(lobby_state.players.size())){const auto&p=lobby_state.players[slot];if(!p.bot)return;a.value=int(LobbyAction::RemoveBot);a.quantity=int(p.id);}
   else a.value=int(LobbyAction::AddBot);
   queue(a);
  });
  listen(menu,("lobby-bot-team-"+std::to_string(slot)).c_str(),"change",[&,slot](auto&){
   if(syncing_controls||slot>=int(lobby_state.players.size())||lobby_state.host!=lobby_player)return;
   const auto&p=lobby_state.players[slot];if(!p.bot)return;
   const auto value=dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("lobby-bot-team-"+std::to_string(slot)))->GetValue();
   UiAction a;a.type=UiActionType::LobbyCommand;a.value=int(LobbyAction::BotTeam);a.quantity=int(p.id)*10+std::stoi(value);queue(a);
  });
 }
 listen(menu,"lobby-address-select","change",[&](auto&){if(!syncing_controls)lobby_address_copied=false;});
 listen(menu,"lobby-invite","click",[&](auto&){
  const auto address=dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("lobby-address-select"))->GetValue();
  if(!address.empty())lobby_address_copied=SDL_SetClipboardText(address.c_str());
 });
 listen(menu,"lobby-start","click",[&](auto&){UiAction a;a.type=UiActionType::LobbyCommand;a.value=int(LobbyAction::Start);queue(a);});
 listen(menu,"lobby-leave","click",[&](auto&){queue({UiActionType::LeaveOnline});});
}
void update_lobby(ClientSession*s){
 const std::pair<const char*,const char*>labels[]={{"open-online","online.title"},{"online-title","online.title"},{"online-name-label","online.name"},{"online-address-label","online.address"},{"online-password-label","online.password"},{"online-capacity-label","online.capacity"},{"online-host","online.host"},{"online-join","online.join"},{"online-resume","online.resume"},{"online-back","settings.back"},{"lobby-title","lobby.title"},{"lobby-team-label","lobby.team"},{"lobby-start","lobby.start"},{"lobby-leave","lobby.leave"}};
 for(auto[id,key]:labels)text(menu,id,localization.text(key));
 text(menu,"online-settings-title",localization.text("online.settings"));text(menu,"online-caption",localization.text("lobby.preview"));text(menu,"online-help",localization.text("online.help"));
 const auto player_name=dynamic_cast<Rml::ElementFormControl*>(menu->GetElementById("online-name"))->GetValue();
 const auto capacity=std::stoi(dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("online-capacity"))->GetValue());
 for(int slot=0;slot<6;++slot){visible(menu,("online-seat-"+std::to_string(slot)).c_str(),slot<capacity);text(menu,("online-seat-name-"+std::to_string(slot)).c_str(),slot?localization.text("lobby.empty"):player_name);text(menu,("online-seat-state-"+std::to_string(slot)).c_str(),localization.text(slot?"online.open_place":"online.your_place"));}

 // A connection handshake is not presentation-ready. If a caller exposed the
 // HUD early, move it back behind the menu until a complete snapshot says
 // whether this client belongs in the lobby or in the running world.
 if(s&&s->remote()&&!s->world_ready()&&world)show_menu();
 if(!s||!s->snapshot_ready()||!s->lobby().enabled){if(showing_lobby){showing_lobby=false;show_menu();}return;}
 lobby_state=s->lobby();lobby_player=s->player_id();
 auto addresses=s->connection_addresses;
 // A joining player may re-share the endpoint they entered. A host without a
 // usable interface must never advertise its loopback self-connection.
 if(addresses.empty()&&lobby_state.host!=lobby_player&&!s->connection_label.empty())
  addresses.push_back({s->connection_label,s->connection_label});
 std::string address_signature=addresses.empty()?"<no-address>\n":std::string{};
 for(const auto&address:addresses)address_signature+=address.endpoint+"\n"+address.label+"\n";
 if(lobby_connection!=address_signature){
  lobby_connection=address_signature;lobby_address_copied=false;
  std::string options;for(const auto&address:addresses)options+="<option value=\""+escaped(address.endpoint)+"\">"+escaped(address.label)+"</option>";
  syncing_controls=true;
  auto*select=dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("lobby-address-select"));
  select->SetInnerRML(options);if(!addresses.empty())select->SetValue(addresses.front().endpoint);
  syncing_controls=false;
 }
 const bool address_available=!addresses.empty();
 for(const char*id:{"lobby-address-select","lobby-invite"}){
  auto*element=menu->GetElementById(id);element->SetPseudoClass("disabled",!address_available);
  if(address_available)element->RemoveAttribute("disabled");else element->SetAttribute("disabled","disabled");
 }
 text(menu,"lobby-address-label",localization.text("lobby.address_pick"));
 text(menu,"lobby-invite",localization.text(!address_available?"lobby.no_address":lobby_address_copied?"lobby.address_copied":"lobby.invite_address"));
 if(lobby_state.started){if(showing_lobby&&s->world_ready()){showing_lobby=false;show_world();}return;}
 if(!showing_lobby){show_menu();showing_lobby=true;}
 visible(menu,"menu-card",false);visible(menu,"online-card",false);visible(menu,"lobby-card",true);
 const auto label=[&](const char*key){return localization.text(key);};
 text(menu,"lobby-title",label("lobby.art_title"));text(menu,"lobby-motto",label("lobby.motto"));
 text(menu,"lobby-settings-title",label("lobby.settings"));text(menu,"lobby-rules",label("lobby.rules"));
 const int own_team=int(s->nation_id());
 const bool shared=std::all_of(lobby_state.players.begin(),lobby_state.players.end(),[&](const auto&p){return p.team==own_team;});
 text(menu,"lobby-coop",localization.format(shared?"lobby.shared_settlement":"lobby.separate_settlements",{{"team",own_team}}));text(menu,"lobby-world-caption",label("lobby.preview"));
 for(int slot=0;slot<6;++slot){
  const auto suffix=std::to_string(slot);visible(menu,("lobby-slot-"+suffix).c_str(),slot<lobby_state.capacity);
  const LobbyPlayer*p=slot<int(lobby_state.players.size())?&lobby_state.players[slot]:nullptr;
  text(menu,("lobby-name-"+suffix).c_str(),p?(p->bot?label("lobby.bot")+" "+std::to_string(p->id):p->name):label("lobby.empty"));
  std::string status=p?label(p->bot?"lobby.bot":p->id==lobby_state.host?"lobby.host":!p->connected?"lobby.disconnected":p->ready?"lobby.is_ready":"lobby.is_not_ready"):label("lobby.waiting_player");
  if(p){status+=" · "+std::to_string(p->team);}
  menu->GetElementById("lobby-state-"+suffix)->SetInnerRML(std::string(p&&p->ready?"<span class=\"ready-check\"></span>":"")+escaped(status));
  const int portrait=p?slot%3:3;
  menu->GetElementById("lobby-portrait-"+suffix)->SetAttribute("rect",std::to_string(portrait*537)+(lobby_state.capacity>=5?" 30 537 680":" 90 537 480"));
  visible(menu,("lobby-slot-action-"+suffix).c_str(),lobby_state.host==lobby_player&&(!p||p->bot));
  text(menu,("lobby-slot-action-"+suffix).c_str(),label(p?"lobby.remove_bot":"lobby.add_bot"));
  visible(menu,("lobby-bot-team-"+suffix).c_str(),p&&p->bot&&lobby_state.host==lobby_player);
  if(p&&p->bot){syncing_controls=true;dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("lobby-bot-team-"+suffix))->SetValue(std::to_string(p->team));syncing_controls=false;}
  if(p&&p->id==lobby_player){syncing_controls=true;dynamic_cast<Rml::ElementFormControlSelect*>(menu->GetElementById("lobby-team"))->SetValue(std::to_string(p->team));syncing_controls=false;text(menu,"lobby-ready",label(p->ready?"lobby.unready":"lobby.ready"));}
 }
 visible(menu,"lobby-start",lobby_state.host==lobby_player);
 text(menu,"lobby-address",localization.text("lobby.network_hint")+"\n"+localization.text("lobby.hint"));
}
