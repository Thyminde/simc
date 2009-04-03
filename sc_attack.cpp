// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simcraft.h"

// ==========================================================================
// Attack
// ==========================================================================

// attack_t::attack_t =======================================================

attack_t::attack_t( const char* n, player_t* p, int resource, int school, int tree, bool special ) :
  action_t( ACTION_ATTACK, n, p, resource, school, tree, special ),
  base_expertise(0), player_expertise(0), target_expertise(0)
{
  may_miss = may_resist = may_dodge = may_parry = may_glance = may_block = may_crit = true;

  if( special ) may_glance = false;

  if( player -> position == POSITION_BACK )
  {
    may_block = false;
    may_parry = false;
  }
  else if( player -> position == POSITION_RANGED )
  {
    may_block  = false;
    may_dodge  = false;
    may_glance = false; // FIXME! If we decide to make ranged auto-shot become "special" this line goes away.
    may_parry  = false;
  }

  base_crit_bonus = 1.0;
  
  direct_power_mod = tick_power_mod = 1.0 / 14;

  trigger_gcd = p -> base_gcd;
  min_gcd = 1.0;
}
  
// attack_t::parse_options =================================================

void attack_t::parse_options( option_t*          options,
			      const std::string& options_str )
{
  option_t base_options[] =
  {
    { "rank",               OPT_INT,    &rank_index            },
    { "seq",                OPT_STRING, &seq_str               },
    { "sync",               OPT_STRING, &sync_str              },
    { "time>",              OPT_FLT,    &min_current_time      },
    { "time<",              OPT_FLT,    &max_current_time      },
    { "time_to_die>",       OPT_FLT,    &min_time_to_die       },
    { "time_to_die<",       OPT_FLT,    &max_time_to_die       },
    { "health_percentage>", OPT_FLT,    &min_health_percentage },
    { "health_percentage<", OPT_FLT,    &max_health_percentage },
    { "bloodlust",          OPT_INT,    &bloodlust_active      },
    { NULL }
  };
  std::vector<option_t> merged_options;
  action_t::parse_options( merge_options( merged_options, options, base_options ), options_str );
}

// attack_t::haste ==========================================================

double attack_t::haste()
{
  player_t* p = player;
  double h = p -> attack_haste;

  if( p -> type != PLAYER_GUARDIAN )
  {
    if( p -> buffs.bloodlust ) 
    {
      h *= 1.0 / ( 1.0 + 0.30 );
    }

    if( p -> buffs.swift_retribution || p -> buffs.improved_moonkin_aura ) 
    {
      h *= 1.0 / ( 1.0 + 0.03 );
    }

    if( p -> position != POSITION_RANGED && p -> buffs.windfury_totem != 0 )
    {
      h *= 1.0 / ( 1.0 + p -> buffs.windfury_totem );
    }

    if( p -> buffs.mongoose_mh ) h *= 1.0 / ( 1.0 + 0.02 );
    if( p -> buffs.mongoose_oh ) h *= 1.0 / ( 1.0 + 0.02 );
  }

  return h;
}

// attack_t::execute_time ===================================================

double attack_t::execute_time()
{
  if( base_execute_time == 0 ) return 0;
  return base_execute_time * haste();
}

// attack_t::player_buff ====================================================

void attack_t::player_buff()
{
  action_t::player_buff();

  player_t* p = player;

  player_hit       = p -> composite_attack_hit();
  player_expertise = p -> composite_attack_expertise();
  player_crit      = p -> composite_attack_crit();
  player_power     = p -> composite_attack_power();
  power_multiplier = p -> composite_attack_power_multiplier();

  if( p -> type != PLAYER_GUARDIAN )
  {
    if( p -> buffs.leader_of_the_pack ) player_crit += 0.05;

    if( p -> gear.chaotic_skyflare      ||
	p -> gear.relentless_earthstorm ) 
    {
      player_crit_multiplier *= 1.03;
    }
  }

  if( sim -> debug ) 
    report_t::log( sim, "attack_t::player_buff: %s hit=%.2f expertise=%.2f crit=%.2f power=%.2f penetration=%.0f", 
		   name(), player_hit, player_expertise, player_crit, player_power, player_penetration );
}

// attack_t::target_debuff ==================================================

void attack_t::target_debuff( int dmg_type )
{
  player_t* p = player;
  target_t* t = sim -> target;

  action_t::target_debuff( dmg_type );

  target_expertise = 0;

  if( p -> position == POSITION_RANGED )
  {
    target_power += t -> debuffs.hunters_mark;
  }
}

// attack_t::miss_chance ====================================================

double attack_t::miss_chance( int delta_level )
{
  if( delta_level > 2 )
  {
    // FIXME: needs testing for delta_level > 3
    return 0.07 + ( delta_level - 2 ) * 0.01 - total_hit();
  }
  else
  {
    return 0.05 + delta_level * 0.005 - total_hit();
  }
}

// attack_t::dodge_chance ===================================================

double attack_t::dodge_chance( int delta_level )
{
  return 0.05 + delta_level * 0.005 - total_expertise();
}

// attack_t::parry_chance ===================================================

double attack_t::parry_chance( int delta_level )
{
  return 0.05 + delta_level * 0.005 - total_expertise();
}

// attack_t::glance_chance ==================================================

double attack_t::glance_chance( int delta_level )
{
  return ( delta_level + 1 ) * 0.06;
}

// attack_t::block_chance ===================================================

double attack_t::block_chance( int delta_level )
{
  return 0.05 + delta_level * 0.005;
}

// attack_t::crit_chance ====================================================

double attack_t::crit_chance( int delta_level )
{
  if( delta_level > 2 )
  {
    return total_crit() - ( 0.03 + delta_level * 0.006 );
  }
  else
  {
    return total_crit() - ( delta_level * 0.002 );
  }
}

// attack_t::build_table ====================================================

int attack_t::build_table( double* chances, 
			   int*    results )
{
  double miss=0, dodge=0, parry=0, glance=0, block=0, crit=0;

  int delta_level = sim -> target -> level - player -> level;

  if( may_miss   )   miss =   miss_chance( delta_level );
  if( may_dodge  )  dodge =  dodge_chance( delta_level );
  if( may_parry  )  parry =  parry_chance( delta_level );
  if( may_glance ) glance = glance_chance( delta_level );

  if( may_block && sim -> target -> shield ) 
  {
    block = block_chance( delta_level );
  }

  if( may_crit ) // && ! special )
  {
    crit = crit_chance( delta_level );
  }

  if( sim -> debug ) report_t::log( sim, "attack_t::build_table: %s miss=%.3f dodge=%.3f parry=%.3f glance=%.3f block=%.3f crit=%.3f",
		   name(), miss, dodge, parry, glance, block, crit );
  
  double total = 0;
  int num_results = 0;

  if( miss > 0 )
  {
    total += miss;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_MISS;
    num_results++;
  }
  if( dodge > 0 )
  {
    total += dodge;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_DODGE;
    num_results++;
  }
  if( parry > 0 )
  {
    total += parry;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_PARRY;
    num_results++;
  }
  if( glance > 0 )
  {
    total += glance;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_GLANCE;
    num_results++;
  }
  if( block > 0 )
  {
    total += block;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_BLOCK;
    num_results++;
  }
  if( crit > 0 )
  {
    total += crit;
    chances[ num_results ] = total;
    results[ num_results ] = RESULT_CRIT;
    num_results++;
  }
  if( total < 1.0 )
  {
    chances[ num_results ] = 1.0;
    results[ num_results ] = RESULT_HIT;
    num_results++;
  }

  return num_results;
}

// attack_t::caclulate_result ===============================================

void attack_t::calculate_result()
{
  direct_dmg = tick_dmg = 0;

  double chances[ RESULT_MAX ];
  int    results[ RESULT_MAX ];

  result = RESULT_NONE;

  int num_results = build_table( chances, results );

  double random = sim -> rng -> real();

  for( int i=0; i < num_results; i++ )
  {
    if( random <= chances[ i ] )
    {
      result = results[ i ];
      break;
    }
  }
  assert( result != RESULT_NONE );

  if( result_is_hit() )
  {
    if( binary && sim -> roll( resistance() ) )
    {
      result = RESULT_RESIST;
    }
    else if( false ) // special && may_crit ) // Specials are 2-roll calculations
    {
      int delta_level = sim -> target -> level - player -> level;

      if( sim -> roll( crit_chance( delta_level ) ) )
      {
	result = RESULT_CRIT;
      }
    }
  }

  for( player_t* p = sim -> player_list; p; p = p -> next )
  {
    if( ! p -> sleeping ) p -> raid_event( this );
  }

  if( sim -> debug ) report_t::log( sim, "%s result for %s is %s", player -> name(), name(), util_t::result_type_string( result ) );
}

