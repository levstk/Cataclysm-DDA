#pragma once
#ifndef PLAYER_H
#define PLAYER_H

#include <climits>
#include <array>
#include <memory>
#include <functional>
#include <iosfwd>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_utility.h"
#include "character.h"
#include "damage.h"
#include "game_constants.h"
#include "item.h"
#include "optional.h"
#include "pimpl.h"
#include "player_activity.h"
#include "ret_val.h"
#include "weighted_list.h"
#include "bodypart.h"
#include "color.h"
#include "creature.h"
#include "cursesdef.h"
#include "inventory.h"
#include "item_location.h"
#include "pldata.h"
#include "type_id.h"
#include "magic.h"
#include "monster.h"
#include "craft_command.h"
#include "point.h"

class basecamp;
class effect;
class faction;
class map;
class npc;
struct pathfinding_settings;
class recipe;
struct islot_comestible;
struct itype;
class monster;

static const std::string DEFAULT_HOTKEYS( "1234567890abcdefghijklmnopqrstuvwxyz" );

class recipe_subset;

enum action_id : int;
struct bionic;
class JsonObject;
class JsonIn;
class JsonOut;
struct dealt_projectile_attack;
class dispersion_sources;

using itype_id = std::string;
using faction_id = string_id<faction>;
struct trap;
class profession;

nc_color encumb_color( int level );
enum game_message_type : int;
class ma_technique;
class martialart;
struct item_comp;
struct tool_comp;
class vehicle;
struct w_point;
struct targeting_data;

// This tries to represent both rating and
// player's decision to respect said rating
enum edible_rating {
    // Edible or we pretend it is
    EDIBLE,
    // Not food at all
    INEDIBLE,
    // Not food because mutated mouth/system
    INEDIBLE_MUTATION,
    // You can eat it, but it will hurt morale
    ALLERGY,
    // Smaller allergy penalty
    ALLERGY_WEAK,
    // Cannibalism (unless psycho/cannibal)
    CANNIBALISM,
    // Rotten or not rotten enough (for saprophages)
    ROTTEN,
    // Can provoke vomiting if you already feel nauseous.
    NAUSEA,
    // We can eat this, but we'll overeat
    TOO_FULL,
    // Some weird stuff that requires a tool we don't have
    NO_TOOL
};

/** @relates ret_val */
template<>
struct ret_val<edible_rating>::default_success : public
    std::integral_constant<edible_rating, EDIBLE> {};

/** @relates ret_val */
template<>
struct ret_val<edible_rating>::default_failure : public
    std::integral_constant<edible_rating, INEDIBLE> {};

enum class rechargeable_cbm {
    none = 0,
    reactor,
    furnace,
    other
};

enum class comfort_level {
    uncomfortable = -999,
    neutral = 0,
    slightly_comfortable = 3,
    comfortable = 5,
    very_comfortable = 10
};

struct special_attack {
    std::string text;
    damage_instance damage;
};

// The maximum level recoil will ever reach.
// This corresponds to the level of accuracy of a "snap" or "hip" shot.
extern const double MAX_RECOIL;

struct stat_mod {
    int strength = 0;
    int dexterity = 0;
    int intelligence = 0;
    int perception = 0;

    int speed = 0;
};

struct needs_rates {
    float thirst;
    float hunger;
    float fatigue;
    float recovery;
    float kcal = 0.0f;
};

class player : public Character
{
    public:
        player();
        player( const player & ) = delete;
        player( player && );
        ~player() override;
        player &operator=( const player & ) = delete;
        player &operator=( player && );

        /** Calls Character::normalize()
         *  normalizes HP and body temperature
         */

        void normalize() override;

        bool is_player() const override {
            return true;
        }
        player *as_player() override {
            return this;
        }
        const player *as_player() const override {
            return this;
        }

        /** Processes human-specific effects of effects before calling Creature::process_effects(). */
        void process_effects() override;
        /** Handles the still hard-coded effects. */
        void hardcoded_effects( effect &it );
        /** Returns the modifier value used for vomiting effects. */
        double vomit_mod();

        bool is_npc() const override {
            return false;    // Overloaded for NPCs in npc.h
        }

        /** Returns what color the player should be drawn as */
        nc_color basic_symbol_color() const override;

        /** Returns an enumeration of visible mutations with colors */
        std::string visible_mutations( int visibility_cap ) const;
        std::vector<std::string> short_description_parts() const;
        std::string short_description() const;
        int print_info( const catacurses::window &w, int vStart, int vLines, int column ) const override;

        // populate variables, inventory items, and misc from json object
        virtual void deserialize( JsonIn &jsin ) = 0;

        // by default save all contained info
        virtual void serialize( JsonOut &jsout ) const = 0;

        /** Handles and displays detailed character info for the '@' screen */
        void disp_info();

        /**Estimate effect duration based on player relevant skill*/
        time_duration estimate_effect_dur( const skill_id &relevant_skill, const efftype_id &effect,
                                           const time_duration &error_magnitude,
                                           int threshold, const Creature &target ) const;

        /** Resets movement points and applies other non-idempotent changes */
        void process_turn() override;
        /** Calculates the various speed bonuses we will get from mutations, etc. */
        void recalc_speed_bonus();
        /** Called after every action, invalidates player caches */
        void action_taken();

        /** Define color for displaying the body temperature */
        nc_color bodytemp_color( int bp ) const;
        /** Returns the player's modified base movement cost */
        int  run_cost( int base_cost, bool diag = false ) const;
        /** Returns the player's speed for swimming across water tiles */
        int  swim_speed() const;
        /** Maintains body wetness and handles the rate at which the player dries */
        void update_body_wetness( const w_point &weather );
        /** Updates all "biology" by one turn. Should be called once every turn. */
        void update_body();
        /** Updates all "biology" as if time between `from` and `to` passed. */
        void update_body( const time_point &from, const time_point &to );
        /** Updates the stomach to give accurate hunger messages */
        void update_stomach( const time_point &from, const time_point &to );
        /** Increases hunger, thirst, fatigue and stimulants wearing off. `rate_multiplier` is for retroactive updates. */
        void update_needs( int rate_multiplier );
        needs_rates calc_needs_rates();

        /**
          * Handles passive regeneration of pain and maybe hp.
          */
        void regen( int rate_multiplier );
        // called once per 24 hours to enforce the minimum of 1 hp healed per day
        // TODO: Move to Character once heal() is moved
        void enforce_minimum_healing();

        /** Kills the player if too hungry, stimmed up etc., forces tired player to sleep and prints warnings. */
        void check_needs_extremes();

        /** Returns if the player has hibernation mutation and is asleep and well fed */
        bool is_hibernating() const;

        /** Returns true if the player has a conflicting trait to the entered trait
         *  Uses has_opposite_trait(), has_lower_trait(), and has_higher_trait() to determine conflicts.
         */
        bool has_conflicting_trait( const trait_id &flag ) const;
        /** Returns true if the player has a trait which cancels the entered trait */
        bool has_opposite_trait( const trait_id &flag ) const;
        /** Returns true if the player has a trait which upgrades into the entered trait */
        bool has_lower_trait( const trait_id &flag ) const;
        /** Returns true if the player has a trait which is an upgrade of the entered trait */
        bool has_higher_trait( const trait_id &flag ) const;
        /** Returns true if the player has a trait that shares a type with the entered trait */
        bool has_same_type_trait( const trait_id &flag ) const;
        /** Returns true if the entered trait may be purified away
         *  Defaults to true
         */
        bool purifiable( const trait_id &flag ) const;
        /** Returns a dream's description selected randomly from the player's highest mutation category */
        std::string get_category_dream( const std::string &cat, int strength ) const;

        /** Handles process of introducing patient into anesthesia during Autodoc operations. Requires anesthetic kits or NOPAIN mutation */
        void introduce_into_anesthesia( const time_duration &duration, player &installer,
                                        bool needs_anesthesia );
        /** Adds a bionic to my_bionics[] */
        void add_bionic( const bionic_id &b );
        /** Removes a bionic from my_bionics[] */
        void remove_bionic( const bionic_id &b );
        /** Calculate skill for (un)installing bionics */
        float bionics_adjusted_skill( const skill_id &most_important_skill,
                                      const skill_id &important_skill,
                                      const skill_id &least_important_skill,
                                      int skill_level = -1 );
        /** Calculate non adjusted skill for (un)installing bionics */
        int bionics_pl_skill( const skill_id &most_important_skill,
                              const skill_id &important_skill,
                              const skill_id &least_important_skill,
                              int skill_level = -1 );
        /**Is the installation possible*/
        bool can_install_bionics( const itype &type, player &installer, bool autodoc = false,
                                  int skill_level = -1 );
        /** Initialize all the values needed to start the operation player_activity */
        bool install_bionics( const itype &type, player &installer, bool autodoc = false,
                              int skill_level = -1 );
        /**Success or failure of installation happens here*/
        void perform_install( bionic_id bid, bionic_id upbid, int difficulty, int success,
                              int pl_skill, std::string installer_name,
                              std::vector<trait_id> trait_to_rem, tripoint patient_pos );
        void bionics_install_failure( bionic_id bid, std::string installer, int difficulty, int success,
                                      float adjusted_skill, tripoint patient_pos );
        /**Is The uninstallation possible*/
        bool can_uninstall_bionic( const bionic_id &b_id, player &installer, bool autodoc = false,
                                   int skill_level = -1 );
        /** Initialize all the values needed to start the operation player_activity */
        bool uninstall_bionic( const bionic_id &b_id, player &installer, bool autodoc = false,
                               int skill_level = -1 );
        /**Succes or failure of removal happens here*/
        void perform_uninstall( bionic_id bid, int difficulty, int success, units::energy power_lvl,
                                int pl_skill );
        /**Used by monster to perform surgery*/
        bool uninstall_bionic( const bionic &target_cbm, monster &installer, player &patient,
                               float adjusted_skill, bool autodoc = false );
        /**When a player fails the surgery*/
        void bionics_uninstall_failure( int difficulty, int success,
                                        float adjusted_skill );
        /**When a monster fails the surgery*/
        void bionics_uninstall_failure( monster &installer, player &patient, int difficulty, int success,
                                        float adjusted_skill );
        /**Has enough anesthetic for surgery*/
        bool has_enough_anesth( const itype *cbm, player &patient );
        /** Generates and handles the UI for player interaction with installed bionics */
        void power_bionics();
        void power_mutations();
        /** Handles bionic activation effects of the entered bionic, returns if anything activated */
        bool activate_bionic( int b, bool eff_only = false );
        /** Handles bionic deactivation effects of the entered bionic, returns if anything deactivated */
        bool deactivate_bionic( int b, bool eff_only = false );
        /**Convert fuel to bionic power*/
        bool burn_fuel( int b, bool start = false );
        /** Handles bionic effects over time of the entered bionic */
        void process_bionic( int b );
        /** Randomly removes a bionic from my_bionics[] */
        bool remove_random_bionic();
        /** Remove all bionics */
        void clear_bionics();
        /** Returns the size of my_bionics[] */
        int num_bionics() const;
        /** Returns amount of Storage CBMs in the corpse **/
        std::pair<int, int> amount_of_storage_bionics() const;
        /** Returns the bionic at a given index in my_bionics[] */
        bionic &bionic_at_index( int i );
        /** Returns the bionic with the given invlet, or NULL if no bionic has that invlet */
        bionic *bionic_by_invlet( int ch );
        /** Returns player luminosity based on the brightest active item they are carrying */
        float active_light() const;

        const tripoint &pos() const override;
        /** Returns the player's sight range */
        int sight_range( int light_level ) const override;
        /** Returns the player maximum vision range factoring in mutations, diseases, and other effects */
        int  unimpaired_range() const;
        /** Returns true if overmap tile is within player line-of-sight */
        bool overmap_los( const tripoint &omt, int sight_points );
        /** Returns the distance the player can see on the overmap */
        int  overmap_sight_range( int light_level ) const;
        /** Returns the distance the player can see through walls */
        int  clairvoyance() const;
        /** Returns true if the player has some form of impaired sight */
        bool sight_impaired() const;
        /** Calculates melee weapon wear-and-tear through use, returns true if item is destroyed. */
        bool handle_melee_wear( item &shield, float wear_multiplier = 1.0f );
        /** Called when a player triggers a trap, returns true if they don't set it off */
        bool avoid_trap( const tripoint &pos, const trap &tr ) const override;

        /** Returns true if the player or their vehicle has an alarm clock */
        bool has_alarm_clock() const;
        /** Returns true if the player or their vehicle has a watch */
        bool has_watch() const;

        // see Creature::sees
        bool sees( const tripoint &t, bool is_player = false, int range_mod = 0 ) const override;
        // see Creature::sees
        bool sees( const Creature &critter ) const override;
        /**
         * Get all hostile creatures currently visible to this player.
         */
        std::vector<Creature *> get_hostile_creatures( int range ) const;

        /**
         * Returns all creatures that this player can see and that are in the given
         * range. This player object itself is never included.
         * The player character (g->u) is checked and might be included (if applicable).
         * @param range The maximal distance (@ref rl_dist), creatures at this distance or less
         * are included.
         */
        std::vector<Creature *> get_visible_creatures( int range ) const;
        /**
         * As above, but includes all creatures the player can detect well enough to target
         * with ranged weapons, e.g. with infrared vision.
         */
        std::vector<Creature *> get_targetable_creatures( int range ) const;
        /**
         * Check whether the this player can see the other creature with infrared. This implies
         * this player can see infrared and the target is visible with infrared (is warm).
         * And of course a line of sight exists.
         */
        bool sees_with_infrared( const Creature &critter ) const;

        Attitude attitude_to( const Creature &other ) const override;

        void pause(); // '.' command; pauses & resets recoil

        // martialarts.cpp

        /** Returns true if the player can learn the entered martial art */
        bool can_autolearn( const matype_id &ma_id ) const;

        /** Returns true if the player is immune to throws */
        bool is_throw_immune() const;
        /** Returns value of player's stable footing */
        float stability_roll() const override;
        /** Returns true if the player has quiet melee attacks */
        bool is_quiet() const;
        /** Returns true if the player has stealthy movement */
        bool is_stealthy() const;
        /** Returns true if the current martial art works with the player's current weapon */
        bool can_melee() const;
        /** Always returns false, since players can't dig currently */
        bool digging() const override;
        /** Returns true if the player is knocked over or has broken legs */
        bool is_on_ground() const override;
        /** Returns true if the player should be dead */
        bool is_dead_state() const override;
        /** Returns true is the player is protected from electric shocks */
        bool is_elec_immune() const override;
        /** Returns true if the player is immune to this kind of effect */
        bool is_immune_effect( const efftype_id & ) const override;
        /** Returns true if the player is immune to this kind of damage */
        bool is_immune_damage( damage_type ) const override;
        /** Returns true if the player is protected from radiation */
        bool is_rad_immune() const;

        /** Returns true if the player is able to use a grab breaking technique */
        bool can_grab_break( const item &weap ) const;
        /** Returns true if the player is able to use a miss recovery technique */
        bool can_miss_recovery( const item &weap ) const;

        // melee.cpp
        /** Returns the best item for blocking with */
        item &best_shield();
        /**
         * Sets up a melee attack and handles melee attack function calls
         * @param t Creature to attack
         * @param allow_special whether non-forced martial art technique or mutation attack should be
         *   possible with this attack.
         * @param force_technique special technique to use in attack.
         * @param allow_unarmed always uses the wielded weapon regardless of martialarts style
         */
        void melee_attack( Creature &t, bool allow_special, const matec_id &force_technique,
                           bool allow_unarmed = true );
        /**
         * Calls the to other melee_attack function with an empty technique id (meaning no specific
         * technique should be used).
         */
        void melee_attack( Creature &t, bool allow_special );

        /**
         * Returns a weapon's modified dispersion value.
         * @param obj Weapon to check dispersion on
         */
        dispersion_sources get_weapon_dispersion( const item &obj ) const;

        /** Returns true if a gun misfires, jams, or has other problems, else returns false */
        bool handle_gun_damage( item &it );

        /** Get maximum recoil penalty due to vehicle motion */
        double recoil_vehicle() const;

        /** Current total maximum recoil penalty from all sources */
        double recoil_total() const;

        /** How many moves does it take to aim gun to the target accuracy. */
        int gun_engagement_moves( const item &gun, int target = 0, int start = MAX_RECOIL ) const;

        /**
         *  Fires a gun or auxiliary gunmod (ignoring any current mode)
         *  @param target where the first shot is aimed at (may vary for later shots)
         *  @param shots maximum number of shots to fire (less may be fired in some circumstances)
         *  @return number of shots actually fired
         */

        int fire_gun( const tripoint &target, int shots = 1 );
        /**
         *  Fires a gun or auxiliary gunmod (ignoring any current mode)
         *  @param target where the first shot is aimed at (may vary for later shots)
         *  @param shots maximum number of shots to fire (less may be fired in some circumstances)
         *  @param gun item to fire (which does not necessary have to be in the players possession)
         *  @return number of shots actually fired
         */
        int fire_gun( const tripoint &target, int shots, item &gun );

        /** Handles reach melee attacks */
        void reach_attack( const tripoint &p );

        /** Checks for valid block abilities and reduces damage accordingly. Returns true if the player blocks */
        bool block_hit( Creature *source, body_part &bp_hit, damage_instance &dam ) override;
        /** Called after the player has successfully dodged an attack */
        void on_dodge( Creature *source, float difficulty ) override;
        /** Handles special defenses from an attack that hit us (source can be null) */
        void on_hit( Creature *source, body_part bp_hit = num_bp,
                     float difficulty = INT_MIN, dealt_projectile_attack const *proj = nullptr ) override;

        /** Returns the bonus bashing damage the player deals based on their stats */
        float bonus_damage( bool random ) const;
        /** Returns weapon skill */
        float get_hit_base() const override;
        /** Returns the player's basic hit roll that is compared to the target's dodge roll */
        float hit_roll() const override;
        /** Returns the chance to critical given a hit roll and target's dodge roll */
        double crit_chance( float roll_hit, float target_dodge, const item &weap ) const;
        /** Returns true if the player scores a critical hit */
        bool scored_crit( float target_dodge, const item &weap ) const;
        /** Returns cost (in moves) of attacking with given item (no modifiers, like stuck) */
        int attack_speed( const item &weap ) const;
        /** Gets melee accuracy component from weapon+skills */
        float get_hit_weapon( const item &weap ) const;
        /** NPC-related item rating functions */
        double weapon_value( const item &weap, int ammo = 10 ) const; // Evaluates item as a weapon
        double gun_value( const item &weap, int ammo = 10 ) const; // Evaluates item as a gun
        double melee_value( const item &weap ) const; // As above, but only as melee
        double unarmed_value() const; // Evaluate yourself!

        // If average == true, adds expected values of random rolls instead of rolling.
        /** Adds all 3 types of physical damage to instance */
        void roll_all_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Adds player's total bash damage to the damage instance */
        void roll_bash_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Adds player's total cut damage to the damage instance */
        void roll_cut_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Adds player's total stab damage to the damage instance */
        void roll_stab_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;

        /** Returns a random valid technique */
        matec_id pick_technique( Creature &t, const item &weap,
                                 bool crit, bool dodge_counter, bool block_counter );
        void perform_technique( const ma_technique &technique, Creature &t, damage_instance &di,
                                int &move_cost );
        /** Performs special attacks and their effects (poisonous, stinger, etc.) */
        void perform_special_attacks( Creature &t );

        /** Returns a vector of valid mutation attacks */
        std::vector<special_attack> mutation_attacks( Creature &t ) const;
        /** Handles combat effects, returns a string of any valid combat effect messages */
        std::string melee_special_effects( Creature &t, damage_instance &d, item &weap );
        /** Returns Creature::get_dodge_base modified by the player's skill level */
        float get_dodge_base() const override;   // Returns the players's dodge, modded by clothing etc
        /** Returns Creature::get_dodge() modified by any player effects */
        float get_dodge() const override;
        /** Returns the player's dodge_roll to be compared against an aggressor's hit_roll() */
        float dodge_roll() override;

        /** Returns melee skill level, to be used to throttle dodge practice. **/
        float get_melee() const override;
        /**
         * Adds a reason for why the player would miss a melee attack.
         *
         * To possibly be messaged to the player when he misses a melee attack.
         * @param reason A message for the player that gives a reason for him missing.
         * @param weight The weight used when choosing what reason to pick when the
         * player misses.
         */
        void add_miss_reason( const std::string &reason, unsigned int weight );
        /** Clears the list of reasons for why the player would miss a melee attack. */
        void clear_miss_reasons();
        /**
         * Returns an explanation for why the player would miss a melee attack.
         */
        std::string get_miss_reason();

        /** Handles the uncanny dodge bionic and effects, returns true if the player successfully dodges */
        bool uncanny_dodge() override;

        /**
         * Checks both the neighborhoods of from and to for climbable surfaces,
         * returns move cost of climbing from `from` to `to`.
         * 0 means climbing is not possible.
         * Return value can depend on the orientation of the terrain.
         */
        int climbing_cost( const tripoint &from, const tripoint &to ) const;

        // ranged.cpp
        /** Execute a throw */
        dealt_projectile_attack throw_item( const tripoint &target, const item &to_throw,
                                            const cata::optional<tripoint> &blind_throw_from_pos = cata::nullopt );

        // Mental skills and stats
        /** Returns the player's reading speed */
        int read_speed( bool return_stat_effect = true ) const;
        /** Returns the player's skill rust rate */
        int rust_rate( bool return_stat_effect = true ) const;
        /** Returns a value used when attempting to convince NPC's of something */
        int talk_skill() const;
        /** Returns a value used when attempting to intimidate NPC's */
        int intimidation() const;

        /**
         * Check if a given body part is immune to a given damage type
         *
         * This function checks whether a given body part cannot be damaged by a given
         * damage_unit.  Note that this refers only to reduction of hp on that part. It
         * does not account for clothing damage, pain, status effects, etc.
         *
         * @param bp: Body part to perform the check on
         * @param dam: Type of damage to check for
         * @returns true if given damage can not reduce hp of given body part
         */
        bool immune_to( body_part bp, damage_unit dam ) const;
        /** Calls Creature::deal_damage and handles damaged effects (waking up, etc.) */
        dealt_damage_instance deal_damage( Creature *source, body_part bp,
                                           const damage_instance &d ) override;
        /** Reduce healing effect intensity, return initial intensity of the effect */
        int reduce_healing_effect( const efftype_id &eff_id, int remove_med, body_part hurt );
        /** Actually hurt the player, hurts a body_part directly, no armor reduction */
        void apply_damage( Creature *source, body_part hurt, int dam,
                           bool bypass_med = false ) override;
        /** Modifies a pain value by player traits before passing it to Creature::mod_pain() */
        void mod_pain( int npain ) override;
        /** Sets new intensity of pain an reacts to it */
        void set_pain( int npain ) override;
        /** Returns perceived pain (reduced with painkillers)*/
        int get_perceived_pain() const override;

        void add_pain_msg( int val, body_part bp ) const;

        /** Knocks the player to a specified tile */
        void knock_back_to( const tripoint &to ) override;

        /** Returns multiplier on fall damage at low velocity (knockback/pit/1 z-level, not 5 z-levels) */
        float fall_damage_mod() const override;
        /** Deals falling/collision damage with terrain/creature at pos */
        int impact( int force, const tripoint &pos ) override;

        /** Returns overall % of HP remaining */
        int hp_percentage() const override;

        /** Handles the chance to be infected by random diseases */
        void get_sick();
        /** Returns list of rc items in player inventory. **/
        std::list<item *> get_radio_items();
        /** Returns list of artifacts in player inventory. **/
        std::list<item *> get_artifact_items();

        /** Adds an addiction to the player */
        void add_addiction( add_type type, int strength );
        /** Removes an addition from the player */
        void rem_addiction( add_type type );
        /** Returns true if the player has an addiction of the specified type */
        bool has_addiction( add_type type ) const;
        /** Returns the intensity of the specified addiction */
        int  addiction_level( add_type type ) const;

        /** Siphons fuel (if available) from the specified vehicle into container or
         * similar via @ref game::handle_liquid. May start a player activity.
         */
        void siphon( vehicle &veh, const itype_id &desired_liquid );
        /** Handles a large number of timers decrementing and other randomized effects */
        void suffer();
        /** Handles mitigation and application of radiation */
        bool irradiate( float rads, bool bypass = false );
        /** Handles the chance for broken limbs to spontaneously heal to 1 HP */
        void mend( int rate_multiplier );

        /** Creates an auditory hallucination */
        void sound_hallu();

        /** Drenches the player with water, saturation is the percent gotten wet */
        void drench( int saturation, const body_part_set &flags, bool ignore_waterproof );
        /** Recalculates morale penalty/bonus from wetness based on mutations, equipment and temperature */
        void apply_wetness_morale( int temperature );

        /** used for drinking from hands, returns how many charges were consumed */
        int drink_from_hands( item &water );
        /** Check whether player can consume this very item */
        bool can_consume_as_is( const item &it ) const;
        /** Used for eating object at pos, returns true if object is removed from inventory (last charge was consumed) */
        bool consume( int target_position );
        /** Used for eating a particular item that doesn't need to be in inventory.
         *  Returns true if the item is to be removed (doesn't remove). */
        bool consume_item( item &target );
        /** Returns allergy type or MORALE_NULL if not allergic for this player */
        morale_type allergy_type( const item &food ) const;
        /** Used for eating entered comestible, returns true if comestible is successfully eaten */
        bool eat( item &food, bool force = false );
        /** Used to apply health modifications from food and medication **/
        void modify_health( const islot_comestible &comest );
        /** Used to apply stimulation modifications from food and medication **/
        void modify_stimulation( const islot_comestible &comest );
        /** Used to apply addiction modifications from food and medication **/
        void modify_addiction( const islot_comestible &comest );

        /** Can the food be [theoretically] eaten no matter the consequences? */
        ret_val<edible_rating> can_eat( const item &food ) const;
        /**
         * Same as @ref can_eat, but takes consequences into account.
         * Asks about them if @param interactive is true, refuses otherwise.
         */
        ret_val<edible_rating> will_eat( const item &food, bool interactive = false ) const;

        // TODO: Move these methods out of the class.
        rechargeable_cbm get_cbm_rechargeable_with( const item &it ) const;
        int get_acquirable_energy( const item &it, rechargeable_cbm cbm ) const;
        int get_acquirable_energy( const item &it ) const;

        /** Gets player's minimum hunger and thirst */
        int stomach_capacity() const;

        /** Handles the kcal value for a comestible **/
        int kcal_for( const item &comest ) const;
        /** Handles the nutrition value for a comestible **/
        int nutrition_for( const item &comest ) const;
        /** Handles the enjoyability value for a book. **/
        int book_fun_for( const item &book, const player &p ) const;
        /**
         * Returns a reference to the item itself (if it's consumable),
         * the first of its contents (if it's consumable) or null item otherwise.
         * WARNING: consumable does not necessarily guarantee the comestible type.
         */
        item &get_consumable_from( item &it ) const;

        std::pair<std::string, nc_color> get_hunger_description() const override;

        std::pair<std::string, nc_color> get_pain_description() const override;

        /** Get vitamin contents for a comestible */
        std::map<vitamin_id, int> vitamins_from( const item &it ) const;
        std::map<vitamin_id, int> vitamins_from( const itype_id &id ) const;

        /** Get vitamin usage rate (minutes per unit) accounting for bionics, mutations and effects */
        time_duration vitamin_rate( const vitamin_id &vit ) const;

        void vitamins_mod( const std::map<vitamin_id, int> &, bool capped = true );

        /**
         * Sets level of a vitamin or returns false if id given in vit does not exist
         *
         * @note status effects are still set for deficiency/excess
         *
         * @param[in] vit ID of vitamin to adjust quantity for
         * @param[in] qty Quantity to set level to
         * @returns false if given vitamin_id does not exist, otherwise true
         */
        bool vitamin_set( const vitamin_id &vit, int qty );

        /** Handles the effects of consuming an item */
        bool consume_effects( item &food );
        int get_lift_assist() const;

        bool list_ammo( const item &base, std::vector<item::reload_option> &ammo_list,
                        bool empty = true ) const;
        /**
         * Select suitable ammo with which to reload the item
         * @param base Item to select ammo for
         * @param prompt force display of the menu even if only one choice
         * @param empty allow selection of empty magazines
         */
        item::reload_option select_ammo( const item &base, bool prompt = false,
                                         bool empty = true ) const;

        /** Select ammo from the provided options */
        item::reload_option select_ammo( const item &base, std::vector<item::reload_option> opts ) const;

        /** Check player strong enough to lift an object unaided by equipment (jacks, levers etc) */
        template <typename T>
        bool can_lift( const T &obj ) const {
            // avoid comparing by weight as different objects use differing scales (grams vs kilograms etc)
            int str = get_str();
            if( mounted_creature ) {
                auto mons = mounted_creature.get();
                str = mons->mech_str_addition() == 0 ? str : mons->mech_str_addition();
            }
            const int npc_str = get_lift_assist();
            if( has_trait( trait_id( "STRONGBACK" ) ) ) {
                str *= 1.35;
            } else if( has_trait( trait_id( "BADBACK" ) ) ) {
                str /= 1.35;
            }
            return str + npc_str >= obj.lift_strength();
        }

        /**
         * Check player capable of wearing an item.
         * @param it Thing to be worn
         */
        ret_val<bool> can_wear( const item &it ) const;

        /**
         * Check player capable of taking off an item.
         * @param it Thing to be taken off
         */
        ret_val<bool> can_takeoff( const item &it, const std::list<item> *res = nullptr ) const;

        /**
         * Check player capable of wielding an item.
         * @param it Thing to be wielded
         */
        ret_val<bool> can_wield( const item &it ) const;
        /**
         * Check player capable of unwielding an item.
         * @param it Thing to be unwielded
         */
        ret_val<bool> can_unwield( const item &it ) const;
        /** Check player's capability of consumption overall */
        bool can_consume( const item &it ) const;

        /** True if the player has enough skill (in cooking or survival) to estimate time to rot */
        bool can_estimate_rot() const;

        bool unwield();

        /**
         * Whether a tool or gun is potentially reloadable (optionally considering a specific ammo)
         * @param it Thing to be reloaded
         * @param ammo if set also check item currently compatible with this specific ammo or magazine
         * @note items currently loaded with a detachable magazine are considered reloadable
         * @note items with integral magazines are reloadable if free capacity permits (+/- ammo matches)
         */
        bool can_reload( const item &it, const itype_id &ammo = std::string() ) const;

        /**
         * Drop, wear, stash or otherwise try to dispose of an item consuming appropriate moves
         * @param obj item to dispose of
         * @param prompt optional message to display in any menu
         * @return whether the item was successfully disposed of
         */
        virtual bool dispose_item( item_location &&obj, const std::string &prompt = std::string() );

        /**
         * Attempt to mend an item (fix any current faults)
         * @param obj Object to mend
         * @param interactive if true prompts player when multiple faults, otherwise mends the first
         */
        void mend_item( item_location &&obj, bool interactive = true );

        /**
         * Calculate (but do not deduct) the number of moves required to reload an item with specified quantity of ammo
         * @param it Item to calculate reload cost for
         * @param ammo either ammo or magazine to use when reloading the item
         * @param qty maximum units of ammo to reload. Capped by remaining capacity and ignored if reloading using a magazine.
         */
        int item_reload_cost( const item &it, const item &ammo, int qty ) const;

        /** Calculate (but do not deduct) the number of moves required to wear an item */
        int item_wear_cost( const item &it ) const;

        /** Wear item; returns false on fail. If interactive is false, don't alert the player or drain moves on completion. */
        cata::optional<std::list<item>::iterator>
        wear( int pos, bool interactive = true );
        cata::optional<std::list<item>::iterator>
        wear( item &to_wear, bool interactive = true );
        /** Wear item; returns nullopt on fail, or pointer to newly worn item on success.
         * If interactive is false, don't alert the player or drain moves on completion.
         */
        cata::optional<std::list<item>::iterator>
        wear_item( const item &to_wear, bool interactive = true );
        /** Swap side on which item is worn; returns false on fail. If interactive is false, don't alert player or drain moves */
        bool change_side( item &it, bool interactive = true );
        bool change_side( int pos, bool interactive = true );

        /** Returns all items that must be taken off before taking off this item */
        std::list<const item *> get_dependent_worn_items( const item &it ) const;
        /** Takes off an item, returning false on fail. The taken off item is processed in the interact */
        bool takeoff( const item &it, std::list<item> *res = nullptr );
        bool takeoff( int pos );
        /** Drops an item to the specified location */
        void drop( int pos, const tripoint &where );
        void drop( const std::list<std::pair<int, int>> &what, const tripoint &target, bool stash = false );

        /** So far only called by unload() from game.cpp */
        bool add_or_drop_with_msg( item &it, bool unloading = false );

        bool unload( item &it );

        /**
         * Try to wield a contained item consuming moves proportional to weapon skill and volume.
         * @param container Container containing the item to be wielded
         * @param pos index of contained item to wield. Set to -1 to show menu if container has more than one item
         * @param penalties Whether item volume and temporary effects (e.g. GRABBED, DOWNED) should be considered.
         * @param base_cost Cost due to storage type.
         */
        bool wield_contents( item &container, int pos = 0, bool penalties = true,
                             int base_cost = INVENTORY_HANDLING_PENALTY );
        /**
         * Stores an item inside another consuming moves proportional to weapon skill and volume
         * @param container Container in which to store the item
         * @param put Item to add to the container
         * @param penalties Whether item volume and temporary effects (e.g. GRABBED, DOWNED) should be considered.
         * @param base_cost Cost due to storage type.
         */
        void store( item &container, item &put, bool penalties = true,
                    int base_cost = INVENTORY_HANDLING_PENALTY );
        /** Draws the UI and handles player input for the armor re-ordering window */
        void sort_armor();
        /** Uses a tool */
        void use( int inventory_position );
        /** Uses a tool at location */
        void use( item_location loc );
        /** Uses the current wielded weapon */
        void use_wielded();
        /**
         * Asks how to use the item (if it has more than one use_method) and uses it.
         * Returns true if it destroys the item. Consumes charges from the item.
         * Multi-use items are ONLY supported when all use_methods are iuse_actor!
         */
        bool invoke_item( item *, const tripoint &pt );
        /** As above, but with a pre-selected method. Debugmsg if this item doesn't have this method. */
        bool invoke_item( item *, const std::string &, const tripoint &pt );
        /** As above two, but with position equal to current position */
        bool invoke_item( item * );
        bool invoke_item( item *, const std::string & );
        /** Reassign letter. */
        void reassign_item( item &it, int invlet );

        /** Consume charges of a tool or comestible item, potentially destroying it in the process
         *  @param used item consuming the charges
         *  @param qty number of charges to consume which must be non-zero
         *  @return true if item was destroyed */
        bool consume_charges( item &used, int qty );

        /** Removes gunmod after first unloading any contained ammo and returns true on success */
        bool gunmod_remove( item &gun, item &mod );

        /** Starts activity to install gunmod having warned user about any risk of failure or irremovable mods s*/
        void gunmod_add( item &gun, item &mod );

        /** @return Odds for success (pair.first) and gunmod damage (pair.second) */
        std::pair<int, int> gunmod_installation_odds( const item &gun, const item &mod ) const;

        /** Starts activity to install toolmod */
        void toolmod_add( item_location tool, item_location mod );

        bool fun_to_read( const item &book ) const;
        /** Note that we've read a book at least once. **/
        virtual bool has_identified( const std::string &item_id ) const = 0;

        /** Handles sleep attempts by the player, starts ACT_TRY_SLEEP activity */
        void try_to_sleep( const time_duration &dur = 30_minutes );
        /** Rate point's ability to serve as a bed. Only takes certain mutations into account, and not fatigue nor stimulants. */
        comfort_level base_comfort_value( const tripoint &p ) const;
        /** Rate point's ability to serve as a bed. Takes all mutations, fatigue and stimulants into account. */
        int sleep_spot( const tripoint &p ) const;
        /** Checked each turn during "lying_down", returns true if the player falls asleep */
        bool can_sleep();

    private:
        /** last time we checked for sleep */
        time_point last_sleep_check = calendar::turn_zero;
        bool bio_soporific_powered_at_last_sleep_check;

    public:
        /** Returns a value from 1.0 to 5.0 that acts as a multiplier
         * for the time taken to perform tasks that require detail vision,
         * above 4.0 means these activities cannot be performed.
         * takes pos as a parameter so that remote spots can be judged
         * if they will potentially have enough light when player gets there */
        float fine_detail_vision_mod( const tripoint &p = tripoint_zero ) const;

        /** Used to determine player feedback on item use for the inventory code.
         *  rates usability lower for non-tools (books, etc.) */
        hint_rating rate_action_use( const item &it ) const;
        hint_rating rate_action_wear( const item &it ) const;
        hint_rating rate_action_change_side( const item &it ) const;
        hint_rating rate_action_eat( const item &it ) const;
        hint_rating rate_action_takeoff( const item &it ) const;
        hint_rating rate_action_reload( const item &it ) const;
        hint_rating rate_action_unload( const item &it ) const;
        hint_rating rate_action_mend( const item &it ) const;
        hint_rating rate_action_disassemble( const item &it );

        //returns true if the warning is now beyond final and results in hostility.
        bool add_faction_warning( const faction_id &id );
        int current_warnings_fac( const faction_id &id );
        bool beyond_final_warning( const faction_id &id );
        /** Returns overall bashing resistance for the body_part */
        int get_armor_bash( body_part bp ) const override;
        /** Returns overall cutting resistance for the body_part */
        int get_armor_cut( body_part bp ) const override;
        /** Returns bashing resistance from the creature and armor only */
        int get_armor_bash_base( body_part bp ) const override;
        /** Returns cutting resistance from the creature and armor only */
        int get_armor_cut_base( body_part bp ) const override;
        /** Returns overall env_resist on a body_part */
        int get_env_resist( body_part bp ) const override;
        /** Returns overall acid resistance for the body part */
        int get_armor_acid( body_part bp ) const;
        /** Returns overall resistance to given type on the bod part */
        int get_armor_type( damage_type dt, body_part bp ) const override;
        /** Returns true if the player is wearing something on the entered body_part, ignoring items with the ALLOWS_NATURAL_ATTACKS flag */
        bool natural_attack_restricted_on( body_part bp ) const;
        /** Returns true if the player is wearing something occupying the helmet slot */
        bool is_wearing_helmet() const;
        /** Returns the total encumbrance of all SKINTIGHT and HELMET_COMPAT items covering the head */
        int head_cloth_encumbrance() const;
        /** Same as footwear factor, but for arms */
        double armwear_factor() const;
        /** Returns 1 if the player is wearing an item of that count on one foot, 2 if on both, and zero if on neither */
        int shoe_type_count( const itype_id &it ) const;
        /** Returns the effect of pain on stats */
        stat_mod get_pain_penalty() const;
        int kcal_speed_penalty();
        /** Returns the penalty to speed from thirst */
        static int thirst_speed_penalty( int thirst );

        int adjust_for_focus( int amount ) const;
        /** This handles giving xp for a skill */
        void practice( const skill_id &id, int amount, int cap = 99, bool suppress_warning = false );
        /** This handles warning the player that there current activity will not give them xp */
        void handle_skill_warning( const skill_id &id, bool force_warning = false );

        void on_worn_item_transform( const item &old_it, const item &new_it );

        /** Get the formatted name of the currently wielded item (if any)
         *  truncated to a number of characters. 0 means it is not truncated
         */
        std::string weapname( unsigned int truncate = 0 ) const;

        float power_rating() const override;
        float speed_rating() const override;

        void process_active_items();
        /**
         * Remove charges from a specific item (given by its item position).
         * The item must exist and it must be counted by charges.
         * @param position Item position of the item.
         * @param quantity The number of charges to remove, must not be larger than
         * the current charges of the item.
         * @return An item that contains the removed charges, it's effectively a
         * copy of the item with the proper charges.
         */
        item reduce_charges( int position, int quantity );
        /**
         * Remove charges from a specific item (given by a pointer to it).
         * Otherwise identical to @ref reduce_charges(int,int)
         * @param it A pointer to the item, it *must* exist.
         * @param quantity How many charges to remove
         */
        item reduce_charges( item *it, int quantity );
        /** Return the item position of the item with given invlet, return INT_MIN if
         * the player does not have such an item with that invlet. Don't use this on npcs.
         * Only use the invlet in the user interface, otherwise always use the item position. */
        int invlet_to_position( int invlet ) const;

        /**
        * Check whether player has a bionic power armor interface.
        * @return true if player has an active bionic capable of powering armor, false otherwise.
        */
        bool can_interface_armor() const;

        // Inventory + weapon + worn (for death, etc)
        std::vector<item *> inv_dump();
        // Put corpse+inventory on map at the place where this is.
        void place_corpse();
        // Put corpse+inventory on defined om tile
        void place_corpse( const tripoint &om_target );

        bool covered_with_flag( const std::string &flag, const body_part_set &parts ) const;
        bool is_waterproof( const body_part_set &parts ) const;

        /** Returns the amount of item `type' that is currently worn */
        int  amount_worn( const itype_id &id ) const;

        // Carried items may leak radiation or chemicals
        int  leak_level( const std::string &flag ) const;

        /** Returns the item in the player's inventory with the highest of the specified quality*/
        item &item_with_best_of_quality( const quality_id &qid );

        /**
        * Prompts user about crushing item at item_location loc, for harvesting of frozen liquids
        * @param loc Location for item to crush
        */
        bool crush_frozen_liquid( item_location loc );

        bool has_mission_item( int mission_id ) const; // Has item with mission_id
        /**
         * Check whether the player has a gun that uses the given type of ammo.
         */
        bool has_gun_for_ammo( const ammotype &at ) const;
        bool has_magazine_for_ammo( const ammotype &at ) const;

        bool has_weapon() const override;

        // Checks crafting inventory for books providing the requested recipe.
        // Then checks nearby NPCs who could provide it too.
        // Returns -1 to indicate recipe not found, otherwise difficulty to learn.
        int has_recipe( const recipe *r, const inventory &crafting_inv,
                        const std::vector<npc *> &helpers ) const;
        bool knows_recipe( const recipe *rec ) const;
        void learn_recipe( const recipe *rec );
        int exceeds_recipe_requirements( const recipe &rec ) const;
        bool has_recipe_requirements( const recipe &rec ) const;
        bool can_decomp_learn( const recipe &rec ) const;

        bool studied_all_recipes( const itype &book ) const;

        /** Returns all known recipes. */
        const recipe_subset &get_learned_recipes() const;
        /** Returns all recipes that are known from the books (either in inventory or nearby). */
        recipe_subset get_recipes_from_books( const inventory &crafting_inv ) const;
        /**
          * Returns all available recipes (from books and npc companions)
          * @param crafting_inv Current available items to craft
          * @param helpers List of NPCs that could help with crafting.
          */
        recipe_subset get_available_recipes( const inventory &crafting_inv,
                                             const std::vector<npc *> *helpers = nullptr ) const;
        /**
          * Returns the set of book types in crafting_inv that provide the
          * given recipe.
          * @param crafting_inv Current available items that may contain readable books
          * @param r Recipe to search for in the available books
          */
        std::set<itype_id> get_books_for_recipe( const inventory &crafting_inv,
                const recipe *r ) const;

        // crafting.cpp
        float morale_crafting_speed_multiplier( const recipe &rec ) const;
        float lighting_craft_speed_multiplier( const recipe &rec ) const;
        float crafting_speed_multiplier( const recipe &rec, bool in_progress = false ) const;
        /** For use with in progress crafts */
        float crafting_speed_multiplier( const item &craft, const tripoint &loc ) const;
        int available_assistant_count( const recipe &rec ) const;
        /**
         * Time to craft not including speed multiplier
         */
        int base_time_to_craft( const recipe &rec, int batch_size = 1 ) const;
        /**
         * Expected time to craft a recipe, with assumption that multipliers stay constant.
         */
        int expected_time_to_craft( const recipe &rec, int batch_size = 1, bool in_progress = false ) const;
        std::vector<const item *> get_eligible_containers_for_crafting() const;
        bool check_eligible_containers_for_crafting( const recipe &rec, int batch_size = 1 ) const;
        bool can_make( const recipe *r, int batch_size = 1 );  // have components?
        /**
         * Returns true if the player can start crafting the recipe with the given batch size
         * The player is not required to have enough tool charges to finish crafting, only to
         * complete the first step (total / 20 + total % 20 charges)
         */
        bool can_start_craft( const recipe *rec, int batch_size = 1 );
        bool making_would_work( const recipe_id &id_to_make, int batch_size );

        /**
         * Start various types of crafts
         * @param loc the location of the workbench. tripoint_zero indicates crafting from inventory.
         */
        void craft( const tripoint &loc = tripoint_zero );
        void recraft( const tripoint &loc = tripoint_zero );
        void long_craft( const tripoint &loc = tripoint_zero );
        void make_craft( const recipe_id &id, int batch_size, const tripoint &loc = tripoint_zero );
        void make_all_craft( const recipe_id &id, int batch_size, const tripoint &loc = tripoint_zero );
        /** consume components and create an active, in progress craft containing them */
        void start_craft( craft_command &command, const tripoint &loc );
        /**
         * Calculate a value representing the success of the player at crafting the given recipe,
         * taking player skill, recipe difficulty, npc helpers, and player mutations into account.
         * @param making the recipe for which to calculate
         * @return a value >= 0.0 with >= 1.0 representing unequivocal success
         */
        double crafting_success_roll( const recipe &making ) const;
        void complete_craft( item &craft, const tripoint &loc = tripoint_zero );
        /**
         * Check if the player meets the requirements to continue the in progress craft and if
         * unable to continue print messages explaining the reason.
         * If the craft is missing components due to messing up, prompt to consume new ones to
         * allow the craft to be continued.
         * @param craft the currently in progress craft
         * @return if the craft can be continued
         */
        bool can_continue_craft( item &craft );
        /** Returns nearby NPCs ready and willing to help with crafting. */
        std::vector<npc *> get_crafting_helpers() const;
        int get_num_crafting_helpers( int max ) const;
        /**
         * Handle skill gain for player and followers during crafting
         * @param craft the currently in progress craft
         * @param multiplier what factor to multiply the base skill gain by.  This is used to apply
         * multiple steps of incremental skill gain simultaneously if needed.
         */
        void craft_skill_gain( const item &craft, const int &multiplier );
        /**
         * Check if the player can disassemble an item using the current crafting inventory
         * @param obj Object to check for disassembly
         * @param inv current crafting inventory
         */
        ret_val<bool> can_disassemble( const item &obj, const inventory &inv ) const;

        bool disassemble();
        bool disassemble( item_location target, bool interactive = true );
        void disassemble_all( bool one_pass ); // Disassemble all items on the tile
        void complete_disassemble();
        void complete_disassemble( item_location &target, const recipe &dis );

        // yet more crafting.cpp
        // includes nearby items
        const inventory &crafting_inventory( const tripoint &src_pos = tripoint_zero,
                                             int radius = PICKUP_RANGE );
        void invalidate_crafting_inventory();
        comp_selection<item_comp>
        select_item_component( const std::vector<item_comp> &components,
                               int batch, inventory &map_inv, bool can_cancel = false,
                               const std::function<bool( const item & )> &filter = return_true<item>, bool player_inv = true );
        std::list<item> consume_items( const comp_selection<item_comp> &is, int batch,
                                       const std::function<bool( const item & )> &filter = return_true<item> );
        std::list<item> consume_items( map &m, const comp_selection<item_comp> &is, int batch,
                                       const std::function<bool( const item & )> &filter = return_true<item>,
                                       const tripoint &origin = tripoint_zero, int radius = PICKUP_RANGE );
        std::list<item> consume_items( const std::vector<item_comp> &components, int batch = 1,
                                       const std::function<bool( const item & )> &filter = return_true<item> );
        comp_selection<tool_comp>
        select_tool_component( const std::vector<tool_comp> &tools, int batch, inventory &map_inv,
                               const std::string &hotkeys = DEFAULT_HOTKEYS,
                               bool can_cancel = false, bool player_inv = true,
        std::function<int( int )> charges_required_modifier = []( int i ) {
            return i;
        } );
        /** Consume tools for the next multiplier * 5% progress of the craft */
        bool craft_consume_tools( item &craft, int mulitplier, bool start_craft );
        void consume_tools( const comp_selection<tool_comp> &tool, int batch );
        void consume_tools( map &m, const comp_selection<tool_comp> &tool, int batch,
                            const tripoint &origin = tripoint_zero, int radius = PICKUP_RANGE,
                            basecamp *bcp = nullptr );
        void consume_tools( const std::vector<tool_comp> &tools, int batch = 1,
                            const std::string &hotkeys = DEFAULT_HOTKEYS );

        // Auto move methods
        void set_destination( const std::vector<tripoint> &route,
                              const player_activity &new_destination_activity = player_activity() );
        void clear_destination();
        bool has_distant_destination() const;

        bool has_destination() const;
        // true if player has destination activity AND is standing on destination tile
        bool has_destination_activity() const;
        // starts destination activity and cleans up to ensure it is called only once
        void start_destination_activity();
        std::vector<tripoint> &get_auto_move_route();
        action_id get_next_auto_move_direction();
        bool defer_move( const tripoint &next );
        void shift_destination( const point &shift );

        // ---------------VALUES-----------------
        tripoint view_offset;
        // Is currently in control of a vehicle
        bool controlling_vehicle;
        // Relative direction of a grab, add to posx, posy to get the coordinates of the grabbed thing.
        tripoint grab_point;
        cata::optional<tripoint> destination_point;
        int volume;
        const profession *prof;

        start_location_id start_location;

        int tank_plut;
        int reactor_plut;
        int slow_rad;
        double recoil = MAX_RECOIL;
        std::weak_ptr<Creature> last_target;
        cata::optional<tripoint> last_target_pos;
        // Save favorite ammo location
        item_location ammo_location;
        int scent;
        int dodges_left;
        int blocks_left;
        int cash;
        int movecounter;
        bool death_drops;// Turned to false for simulating NPCs on distant missions so they don't drop all their gear in sight

        int focus_pool;

        bool reach_attacking = false;
        bool manual_examine = false;

        std::vector <addiction> addictions;
        std::vector<mtype_id> starting_pets;

        void make_craft_with_command( const recipe_id &id_to_make, int batch_size, bool is_long = false,
                                      const tripoint &loc = tripoint_zero );
        pimpl<craft_command> last_craft;

        recipe_id lastrecipe;
        int last_batch;
        itype_id lastconsumed;        //used in crafting.cpp and construction.cpp

        int get_used_bionics_slots( body_part bp ) const;
        int get_total_bionics_slots( body_part bp ) const;
        int get_free_bionics_slots( body_part bp ) const;
        std::map<body_part, int> bionic_installation_issues( const bionic_id &bioid );

        std::set<character_id> follower_ids;
        void mod_stat( const std::string &stat, float modifier ) override;

        bool is_underwater() const override;
        void set_underwater( bool );
        bool is_hallucination() const override;
        void environmental_revert_effect();

        bool is_deaf() const;
        // Checks whether a player can hear a sound at a given volume and location.
        bool can_hear( const tripoint &source, int volume ) const;
        // Returns a multiplier indicating the keenness of a player's hearing.
        float hearing_ability() const;

        m_size get_size() const override;
        int get_hp( hp_part bp ) const override;
        int get_hp() const override;
        int get_hp_max( hp_part bp ) const override;
        int get_hp_max() const override;

        //message related stuff
        using Character::add_msg_if_player;
        void add_msg_if_player( const std::string &msg ) const override;
        void add_msg_if_player( game_message_type type, const std::string &msg ) const override;
        using Character::add_msg_player_or_npc;
        void add_msg_player_or_npc( const std::string &player_msg,
                                    const std::string &npc_str ) const override;
        void add_msg_player_or_npc( game_message_type type, const std::string &player_msg,
                                    const std::string &npc_msg ) const override;
        using Character::add_msg_player_or_say;
        void add_msg_player_or_say( const std::string &player_msg,
                                    const std::string &npc_speech ) const override;
        void add_msg_player_or_say( game_message_type type, const std::string &player_msg,
                                    const std::string &npc_speech ) const override;

        using trap_map = std::map<tripoint, std::string>;
        bool knows_trap( const tripoint &pos ) const;
        void add_known_trap( const tripoint &pos, const trap &t );
        /** Search surrounding squares for traps (and maybe other things in the future). */
        void search_surroundings();

        // TODO: make protected and move into Character
        void do_skill_rust();

        /**
         * Called when a mutation is gained
         */
        void on_mutation_gain( const trait_id &mid ) override;
        /**
         * Called when a mutation is lost
         */
        void on_mutation_loss( const trait_id &mid ) override;
        /**
         * Called when a stat is changed
         */
        void on_stat_change( const std::string &stat, int value ) override;
        /**
         * Called when an item is worn
         */
        void on_item_wear( const item &it ) override;
        /**
         * Called when an item is taken off
         */
        void on_item_takeoff( const item &it ) override;
        /**
         * Called when an item is washed
         */
        void on_worn_item_washed( const item &it ) override;
        /**
         * Called when effect intensity has been changed
         */
        void on_effect_int_change( const efftype_id &eid, int intensity, body_part bp = num_bp ) override;

        // formats and prints encumbrance info to specified window
        void print_encumbrance( const catacurses::window &win, int line = -1,
                                const item *selected_clothing = nullptr ) const;

        // Prints message(s) about current health
        void print_health() const;

        using Character::query_yn;
        bool query_yn( const std::string &mes ) const override;

        /**
         * Has the item enough charges to invoke its use function?
         * Also checks if UPS from this player is used instead of item charges.
         */
        bool has_enough_charges( const item &it, bool show_msg ) const;

        const pathfinding_settings &get_pathfinding_settings() const override;
        std::set<tripoint> get_path_avoid() const override;

        /**
         * Try to disarm the NPC. May result in fail attempt, you receiving the wepon and instantly wielding it,
         * or the weapon falling down on the floor nearby. NPC is always getting angry with you.
         * @param target Target NPC to disarm
         */
        void disarm( npc &target );

        /**
         * Accessor method for weapon targeting data, used for interactive weapon aiming.
         * @return a reference to the data pointed by player's tdata member.
         */
        const targeting_data &get_targeting_data();

        /**
         * Mutator method for weapon targeting data.
         * @param td targeting data to be set.
         */
        void set_targeting_data( const targeting_data &td );

        std::set<tripoint> camps;

    protected:

        trap_map known_traps;

        void store( JsonOut &json ) const;
        void load( JsonObject &data );

        /** Processes human-specific effects of an effect. */
        void process_one_effect( effect &it, bool is_new ) override;

    private:

        /** Check if an area-of-effect technique has valid targets */
        bool valid_aoe_technique( Creature &t, const ma_technique &technique );
        bool valid_aoe_technique( Creature &t, const ma_technique &technique,
                                  std::vector<Creature *> &targets );
        /**
         * Check whether the other creature is in range and can be seen by this creature.
         * @param critter Creature to check for visibility
         * @param range The maximal distance (@ref rl_dist), creatures at this distance or less
         * are included.
         */
        bool is_visible_in_range( const Creature &critter, int range ) const;

        // Trigger and disable mutations that can be so toggled.
        void activate_mutation( const trait_id &mutation );
        void deactivate_mutation( const trait_id &mut );

        /** Determine player's capability of recharging their CBMs. */
        bool can_feed_reactor_with( const item &it ) const;
        bool can_feed_furnace_with( const item &it ) const;
        /**
         * Recharge CBMs whenever possible.
         * @return true when recharging was successful.
         */
        bool feed_reactor_with( item &it );
        bool feed_furnace_with( item &it );
        bool fuel_bionic_with( item &it );
        /**
         * Consumes an item as medication.
         * @param target Item consumed. Must be a medication or a container of medication.
         * @return Whether the target was fully consumed.
         */
        bool consume_med( item &target );

    private:

        std::vector<tripoint> auto_move_route;
        // Used to make sure auto move is canceled if we stumble off course
        cata::optional<tripoint> next_expected_position;
        /** warnings from a faction about bad behaviour */
        std::map<faction_id, std::pair<int, time_point>> warning_record;
        inventory cached_crafting_inventory;
        int cached_moves;
        time_point cached_time;
        tripoint cached_position;

    private:

        struct weighted_int_list<std::string> melee_miss_reasons;

        /** smart pointer to targeting data stored for aiming the player's weapon across turns. */
        std::shared_ptr<targeting_data> tdata;

    protected:

        /** Subset of learned recipes. Needs to be mutable for lazy initialization. */
        mutable pimpl<recipe_subset> learned_recipes;

        /** Stamp of skills. @ref learned_recipes are valid only with this set of skills. */
        mutable decltype( _skills ) valid_autolearn_skills;
};

#endif
