/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Philip Withnall <withnall@endlessm.com>
 */

#include "config.h"

#include <glib.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <libeos-parental-controls/app-filter.h>
#include <libglib-testing/dbus-queue.h>
#include <locale.h>
#include <string.h>
#include "accounts-service-iface.h"


/* Check two arrays contain exactly the same items in the same order. */
static void
assert_strv_equal (const gchar * const *strv_a,
                   const gchar * const *strv_b)
{
  gsize i;

  for (i = 0; strv_a[i] != NULL && strv_b[i] != NULL; i++)
    g_assert_cmpstr (strv_a[i], ==, strv_b[i]);

  g_assert_null (strv_a[i]);
  g_assert_null (strv_b[i]);
}

/* FIXME: Use g_assert_cmpvariant() when
 * https://gitlab.gnome.org/GNOME/glib/issues/1191 is fixed. */
#define assert_cmpvariant(v1, v2) \
  G_STMT_START \
  { \
    GVariant *__v1 = (v1), *__v2 = (v2); \
    if (!g_variant_equal (__v1, __v2)) \
      { \
        gchar *__s1, *__s2, *__msg; \
        __s1 = g_variant_print (__v1, TRUE); \
        __s2 = g_variant_print (__v2, TRUE); \
        __msg = g_strdup_printf ("assertion failed (" #v1 " == " #v2 "): %s does not equal %s", __s1, __s2); \
        g_assertion_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, __msg); \
        g_free (__s1); \
        g_free (__s2); \
        g_free (__msg); \
      } \
  } \
  G_STMT_END


/* A placeholder smoketest which checks that the error quark works. */
static void
test_app_filter_error_quark (void)
{
  g_assert_cmpint (epc_app_filter_error_quark (), !=, 0);
}

/* Test that the #GType definitions for various types work. */
static void
test_app_filter_types (void)
{
  g_type_ensure (epc_app_filter_get_type ());
  g_type_ensure (epc_app_filter_builder_get_type ());
}

/* Test that ref() and unref() work on an #EpcAppFilter. */
static void
test_app_filter_refs (void)
{
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) filter = NULL;

  /* Use an empty #EpcAppFilter. */
  filter = epc_app_filter_builder_end (&builder);

  g_assert_nonnull (filter);

  /* Call is_path_allowed() to check that the filter hasn’t been finalised. */
  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));
  epc_app_filter_ref (filter);
  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));
  epc_app_filter_unref (filter);
  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));

  /* Final ref is dropped by g_autoptr(). */
}

/* Fixture for tests which use an #EpcAppFilterBuilder. The builder can either
 * be heap- or stack-allocated. @builder will always be a valid pointer to it.
 */
typedef struct
{
  EpcAppFilterBuilder *builder;
  EpcAppFilterBuilder stack_builder;
} BuilderFixture;

static void
builder_set_up_stack (BuilderFixture *fixture,
                      gconstpointer   test_data)
{
  epc_app_filter_builder_init (&fixture->stack_builder);
  fixture->builder = &fixture->stack_builder;
}

static void
builder_tear_down_stack (BuilderFixture *fixture,
                         gconstpointer   test_data)
{
  epc_app_filter_builder_clear (&fixture->stack_builder);
  fixture->builder = NULL;
}

static void
builder_set_up_stack2 (BuilderFixture *fixture,
                       gconstpointer   test_data)
{
  EpcAppFilterBuilder local_builder = EPC_APP_FILTER_BUILDER_INIT ();
  memcpy (&fixture->stack_builder, &local_builder, sizeof (local_builder));
  fixture->builder = &fixture->stack_builder;
}

static void
builder_tear_down_stack2 (BuilderFixture *fixture,
                          gconstpointer   test_data)
{
  epc_app_filter_builder_clear (&fixture->stack_builder);
  fixture->builder = NULL;
}

static void
builder_set_up_heap (BuilderFixture *fixture,
                     gconstpointer   test_data)
{
  fixture->builder = epc_app_filter_builder_new ();
}

static void
builder_tear_down_heap (BuilderFixture *fixture,
                        gconstpointer   test_data)
{
  g_clear_pointer (&fixture->builder, epc_app_filter_builder_free);
}

/* Test building a non-empty #EpcAppFilter using an #EpcAppFilterBuilder. */
static void
test_app_filter_builder_non_empty (BuilderFixture *fixture,
                                   gconstpointer   test_data)
{
  g_autoptr(EpcAppFilter) filter = NULL;
  g_autofree const gchar **sections = NULL;

  epc_app_filter_builder_blacklist_path (fixture->builder, "/bin/true");
  epc_app_filter_builder_blacklist_path (fixture->builder, "/usr/bin/gnome-software");

  epc_app_filter_builder_blacklist_flatpak_ref (fixture->builder,
                                                "app/org.doom.Doom/x86_64/master");

  epc_app_filter_builder_set_oars_value (fixture->builder, "drugs-alcohol",
                                         EPC_APP_FILTER_OARS_VALUE_MILD);
  epc_app_filter_builder_set_oars_value (fixture->builder, "language-humor",
                                         EPC_APP_FILTER_OARS_VALUE_MODERATE);
  epc_app_filter_builder_set_allow_user_installation (fixture->builder, TRUE);
  epc_app_filter_builder_set_allow_system_installation (fixture->builder, FALSE);

  filter = epc_app_filter_builder_end (fixture->builder);

  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));
  g_assert_false (epc_app_filter_is_path_allowed (filter,
                                                  "/usr/bin/gnome-software"));

  g_assert_true (epc_app_filter_is_flatpak_ref_allowed (filter,
                                                        "app/org.gnome.Ponies/x86_64/master"));
  g_assert_true (epc_app_filter_is_flatpak_app_allowed (filter, "org.gnome.Ponies"));
  g_assert_false (epc_app_filter_is_flatpak_ref_allowed (filter,
                                                         "app/org.doom.Doom/x86_64/master"));
  g_assert_false (epc_app_filter_is_flatpak_app_allowed (filter, "org.doom.Doom"));

  g_assert_cmpint (epc_app_filter_get_oars_value (filter, "drugs-alcohol"), ==,
                   EPC_APP_FILTER_OARS_VALUE_MILD);
  g_assert_cmpint (epc_app_filter_get_oars_value (filter, "language-humor"), ==,
                   EPC_APP_FILTER_OARS_VALUE_MODERATE);
  g_assert_cmpint (epc_app_filter_get_oars_value (filter, "something-else"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);

  sections = epc_app_filter_get_oars_sections (filter);
  const gchar * const expected_sections[] = { "drugs-alcohol", "language-humor", NULL };
  assert_strv_equal ((const gchar * const *) sections, expected_sections);

  g_assert_true (epc_app_filter_is_user_installation_allowed (filter));
  g_assert_false (epc_app_filter_is_system_installation_allowed (filter));
}

/* Test building an empty #EpcAppFilter using an #EpcAppFilterBuilder. */
static void
test_app_filter_builder_empty (BuilderFixture *fixture,
                               gconstpointer   test_data)
{
  g_autoptr(EpcAppFilter) filter = NULL;
  g_autofree const gchar **sections = NULL;

  filter = epc_app_filter_builder_end (fixture->builder);

  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));
  g_assert_true (epc_app_filter_is_path_allowed (filter,
                                                 "/usr/bin/gnome-software"));

  g_assert_true (epc_app_filter_is_flatpak_ref_allowed (filter,
                                                        "app/org.gnome.Ponies/x86_64/master"));
  g_assert_true (epc_app_filter_is_flatpak_app_allowed (filter, "org.gnome.Ponies"));
  g_assert_true (epc_app_filter_is_flatpak_ref_allowed (filter,
                                                        "app/org.doom.Doom/x86_64/master"));
  g_assert_true (epc_app_filter_is_flatpak_app_allowed (filter, "org.doom.Doom"));

  g_assert_cmpint (epc_app_filter_get_oars_value (filter, "drugs-alcohol"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);
  g_assert_cmpint (epc_app_filter_get_oars_value (filter, "language-humor"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);
  g_assert_cmpint (epc_app_filter_get_oars_value (filter, "something-else"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);

  sections = epc_app_filter_get_oars_sections (filter);
  const gchar * const expected_sections[] = { NULL };
  assert_strv_equal ((const gchar * const *) sections, expected_sections);

  g_assert_true (epc_app_filter_is_user_installation_allowed (filter));
  g_assert_false (epc_app_filter_is_system_installation_allowed (filter));
}

/* Check that copying a cleared #EpcAppFilterBuilder works, and the copy can
 * then be initialised and used to build a filter. */
static void
test_app_filter_builder_copy_empty (void)
{
  g_autoptr(EpcAppFilterBuilder) builder = epc_app_filter_builder_new ();
  g_autoptr(EpcAppFilterBuilder) builder_copy = NULL;
  g_autoptr(EpcAppFilter) filter = NULL;

  epc_app_filter_builder_clear (builder);
  builder_copy = epc_app_filter_builder_copy (builder);

  epc_app_filter_builder_init (builder_copy);
  epc_app_filter_builder_blacklist_path (builder_copy, "/bin/true");
  filter = epc_app_filter_builder_end (builder_copy);

  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));
  g_assert_false (epc_app_filter_is_path_allowed (filter, "/bin/true"));

  g_assert_true (epc_app_filter_is_user_installation_allowed (filter));
  g_assert_false (epc_app_filter_is_system_installation_allowed (filter));
}

/* Check that copying a filled #EpcAppFilterBuilder works, and the copy can be
 * used to build a filter. */
static void
test_app_filter_builder_copy_full (void)
{
  g_autoptr(EpcAppFilterBuilder) builder = epc_app_filter_builder_new ();
  g_autoptr(EpcAppFilterBuilder) builder_copy = NULL;
  g_autoptr(EpcAppFilter) filter = NULL;

  epc_app_filter_builder_blacklist_path (builder, "/bin/true");
  epc_app_filter_builder_set_allow_user_installation (builder, FALSE);
  epc_app_filter_builder_set_allow_system_installation (builder, TRUE);
  builder_copy = epc_app_filter_builder_copy (builder);
  filter = epc_app_filter_builder_end (builder_copy);

  g_assert_true (epc_app_filter_is_path_allowed (filter, "/bin/false"));
  g_assert_false (epc_app_filter_is_path_allowed (filter, "/bin/true"));
  g_assert_false (epc_app_filter_is_user_installation_allowed (filter));
  g_assert_true (epc_app_filter_is_system_installation_allowed (filter));
}

/* Check that various configurations of a #GAppInfo are accepted or rejected
 * as appropriate by epc_app_filter_is_appinfo_allowed(). */
static void
test_app_filter_appinfo (void)
{
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) filter = NULL;
  const struct
    {
      gboolean expected_allowed;
      const gchar *key_file_data;
    }
  vectors[] =
    {
      /* Allowed by its path: */
      { TRUE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n" },
      /* Allowed by its path and its flatpak ID: */
      { TRUE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak=org.gnome.Nice\n" },
      /* Allowed by its path and its flatpak ID: */
      { TRUE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak=org.gnome.Nice\n"
        "X-Flatpak-RenamedFrom=\n" },
      /* Allowed by its path, its flatpak ID and its old flatpak IDs: */
      { TRUE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak-RenamedFrom=org.gnome.OldNice\n" },
      /* Allowed by its path, its flatpak ID and its old flatpak IDs (which
       * contain some spurious entries): */
      { TRUE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak-RenamedFrom=org.gnome.OldNice;;;\n" },
      /* Allowed by its path, its flatpak ID and its old flatpak IDs: */
      { TRUE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak-RenamedFrom=org.gnome.OldNice.desktop\n" },
      /* Disallowed by its path: */
      { FALSE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/false\n"
        "Type=Application\n" },
      /* Allowed by its path, disallowed by its flatpak ID: */
      { FALSE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak=org.gnome.Nasty\n" },
      /* Allowed by its path and current flatpak ID, but disallowed by an old
       * flatpak ID: */
      { FALSE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak=org.gnome.WasNasty\n"
        "X-Flatpak-RenamedFrom= org.gnome.OlderNasty ; org.gnome.Nasty ; \n" },
      /* Allowed by its path and current flatpak ID, but disallowed by an old
       * flatpak ID: */
      { FALSE,
        "[Desktop Entry]\n"
        "Name=Some Name\n"
        "Exec=/bin/true\n"
        "Type=Application\n"
        "X-Flatpak=org.gnome.WasNasty\n"
        "X-Flatpak-RenamedFrom=org.gnome.Nasty.desktop;\n" },
    };

  epc_app_filter_builder_blacklist_path (&builder, "/bin/false");
  epc_app_filter_builder_blacklist_flatpak_ref (&builder, "app/org.gnome.Nasty/x86_64/stable");

  filter = epc_app_filter_builder_end (&builder);

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GKeyFile) key_file = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autoptr(GAppInfo) appinfo = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": %s",
                      i, vectors[i].key_file_data);

      key_file = g_key_file_new ();
      g_key_file_load_from_data (key_file, vectors[i].key_file_data, -1,
                                 G_KEY_FILE_NONE, &local_error);
      g_assert_no_error (local_error);

      appinfo = G_APP_INFO (g_desktop_app_info_new_from_keyfile (key_file));
      g_assert_nonnull (appinfo);

      if (vectors[i].expected_allowed)
        g_assert_true (epc_app_filter_is_appinfo_allowed (filter, appinfo));
      else
        g_assert_false (epc_app_filter_is_appinfo_allowed (filter, appinfo));
    }
}

/* Fixture for tests which interact with the accountsservice over D-Bus. The
 * D-Bus service is mocked up using @queue, which allows us to reply to D-Bus
 * calls from the code under test from within the test process.
 *
 * It exports one user object (for UID 500) and the manager object. The method
 * return values from UID 500 are up to the test in question, so it could be an
 * administrator, or non-administrator, have a restrictive or permissive app
 * filter, etc.
 */
typedef struct
{
  GtDBusQueue *queue;  /* (owned) */
  uid_t valid_uid;
  uid_t missing_uid;
} BusFixture;

static void
bus_set_up (BusFixture    *fixture,
            gconstpointer  test_data)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *object_path = NULL;

  fixture->valid_uid = 500;  /* arbitrarily chosen */
  fixture->missing_uid = 501;  /* must be different from valid_uid and not exported */
  fixture->queue = gt_dbus_queue_new ();

  gt_dbus_queue_connect (fixture->queue, &local_error);
  g_assert_no_error (local_error);

  gt_dbus_queue_own_name (fixture->queue, "org.freedesktop.Accounts");

  object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%u", fixture->valid_uid);
  gt_dbus_queue_export_object (fixture->queue,
                               object_path,
                               (GDBusInterfaceInfo *) &app_filter_interface_info,
                               &local_error);
  g_assert_no_error (local_error);

  gt_dbus_queue_export_object (fixture->queue,
                               "/org/freedesktop/Accounts",
                               (GDBusInterfaceInfo *) &accounts_interface_info,
                               &local_error);
  g_assert_no_error (local_error);
}

static void
bus_tear_down (BusFixture    *fixture,
               gconstpointer  test_data)
{
  gt_dbus_queue_disconnect (fixture->queue, TRUE);
  g_clear_pointer (&fixture->queue, gt_dbus_queue_free);
}

/* Helper #GAsyncReadyCallback which returns the #GAsyncResult in its @user_data. */
static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = (GAsyncResult **) user_data;

  g_assert_null (*result_out);
  *result_out = g_object_ref (result);
}

/* Generic mock accountsservice implementation which returns the properties
 * given in #GetAppFilterData.properties if queried for a UID matching
 * #GetAppFilterData.expected_uid. Intended to be used for writing ‘successful’
 * epc_get_app_filter() tests returning a variety of values. */
typedef struct
{
  uid_t expected_uid;
  const gchar *properties;
} GetAppFilterData;

/* This is run in a worker thread. */
static void
get_app_filter_server_cb (GtDBusQueue *queue,
                          gpointer     user_data)
{
  const GetAppFilterData *data = user_data;
  g_autoptr(GDBusMethodInvocation) invocation1 = NULL;
  g_autoptr(GDBusMethodInvocation) invocation2 = NULL;
  g_autofree gchar *object_path = NULL;
  g_autoptr(GVariant) properties_variant = NULL;

  /* Handle the FindUserById() call. */
  gint64 user_id;
  invocation1 =
      gt_dbus_queue_assert_pop_message (queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, data->expected_uid);

  object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%u", (uid_t) user_id);
  g_dbus_method_invocation_return_value (invocation1, g_variant_new ("(o)", object_path));

  /* Handle the Properties.GetAll() call and return some arbitrary, valid values
   * for the given user. */
  const gchar *property_interface;
  invocation2 =
      gt_dbus_queue_assert_pop_message (queue,
                                        object_path,
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll", "(&s)", &property_interface);
  g_assert_cmpstr (property_interface, ==, "com.endlessm.ParentalControls.AppFilter");

  properties_variant = g_variant_ref_sink (g_variant_new_parsed (data->properties));
  g_dbus_method_invocation_return_value (invocation2,
                                         g_variant_new_tuple (&properties_variant, 1));
}

/* Test that getting an #EpcAppFilter from the mock D-Bus service works. The
 * @test_data is a boolean value indicating whether to do the call
 * synchronously (%FALSE) or asynchronously (%TRUE).
 *
 * The mock D-Bus replies are generated in get_app_filter_server_cb(), which is
 * used for both synchronous and asynchronous calls. */
static void
test_app_filter_bus_get (BusFixture    *fixture,
                         gconstpointer  test_data)
{
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  gboolean test_async = GPOINTER_TO_UINT (test_data);
  const GetAppFilterData get_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .properties = "{"
        "'AllowUserInstallation': <true>,"
        "'AllowSystemInstallation': <false>,"
        "'AppFilter': <(false, ['app/org.gnome.Builder/x86_64/stable'])>,"
        "'OarsFilter': <('oars-1.1', { 'violence-bloodshed': 'mild' })>"
      "}"
    };

  gt_dbus_queue_set_server_func (fixture->queue, get_app_filter_server_cb,
                                 (gpointer) &get_app_filter_data);

  if (test_async)
    {
      g_autoptr(GAsyncResult) result = NULL;

      epc_get_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                                fixture->valid_uid,
                                FALSE, NULL, async_result_cb, &result);

      while (result == NULL)
        g_main_context_iteration (NULL, TRUE);
      app_filter = epc_get_app_filter_finish (result, &local_error);
    }
  else
    {
      app_filter = epc_get_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                       fixture->valid_uid,
                                       FALSE, NULL, &local_error);
    }

  g_assert_no_error (local_error);
  g_assert_nonnull (app_filter);

  /* Check the app filter properties. */
  g_assert_cmpuint (epc_app_filter_get_user_id (app_filter), ==, fixture->valid_uid);
  g_assert_false (epc_app_filter_is_flatpak_app_allowed (app_filter, "org.gnome.Builder"));
  g_assert_true (epc_app_filter_is_flatpak_app_allowed (app_filter, "org.gnome.Chess"));
}

/* Test that getting an #EpcAppFilter containing a whitelist from the mock D-Bus
 * service works, and that the #EpcAppFilter methods handle the whitelist
 * correctly.
 *
 * The mock D-Bus replies are generated in get_app_filter_server_cb(). */
static void
test_app_filter_bus_get_whitelist (BusFixture    *fixture,
                                   gconstpointer  test_data)
{
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  const GetAppFilterData get_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .properties = "{"
        "'AllowUserInstallation': <true>,"
        "'AllowSystemInstallation': <true>,"
        "'AppFilter': <(true, ["
          "'app/org.gnome.Whitelisted1/x86_64/stable',"
          "'app/org.gnome.Whitelisted2/x86_64/stable',"
          "'/usr/bin/true'"
        "])>,"
        "'OarsFilter': <('oars-1.1', @a{ss} {})>"
      "}"
    };

  gt_dbus_queue_set_server_func (fixture->queue, get_app_filter_server_cb,
                                 (gpointer) &get_app_filter_data);

  app_filter = epc_get_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                   fixture->valid_uid,
                                   FALSE, NULL, &local_error);

  g_assert_no_error (local_error);
  g_assert_nonnull (app_filter);

  /* Check the app filter properties. The returned filter is a whitelist,
   * whereas typically a blacklist is returned. */
  g_assert_cmpuint (epc_app_filter_get_user_id (app_filter), ==, fixture->valid_uid);
  g_assert_false (epc_app_filter_is_flatpak_app_allowed (app_filter, "org.gnome.Builder"));
  g_assert_true (epc_app_filter_is_flatpak_app_allowed (app_filter, "org.gnome.Whitelisted1"));
  g_assert_true (epc_app_filter_is_flatpak_app_allowed (app_filter, "org.gnome.Whitelisted2"));
  g_assert_true (epc_app_filter_is_flatpak_ref_allowed (app_filter, "app/org.gnome.Whitelisted1/x86_64/stable"));
  g_assert_false (epc_app_filter_is_flatpak_ref_allowed (app_filter, "app/org.gnome.Whitelisted1/x86_64/unknown"));
  g_assert_true (epc_app_filter_is_path_allowed (app_filter, "/usr/bin/true"));
  g_assert_false (epc_app_filter_is_path_allowed (app_filter, "/usr/bin/false"));
}

/* Test that getting an #EpcAppFilter containing all possible OARS values from
 * the mock D-Bus service works, and that the #EpcAppFilter methods handle them
 * correctly.
 *
 * The mock D-Bus replies are generated in get_app_filter_server_cb(). */
static void
test_app_filter_bus_get_all_oars_values (BusFixture    *fixture,
                                         gconstpointer  test_data)
{
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  const GetAppFilterData get_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .properties = "{"
        "'AllowUserInstallation': <true>,"
        "'AllowSystemInstallation': <true>,"
        "'AppFilter': <(false, @as [])>,"
        "'OarsFilter': <('oars-1.1', {"
          "'violence-bloodshed': 'none',"
          "'violence-sexual': 'mild',"
          "'violence-fantasy': 'moderate',"
          "'violence-realistic': 'intense',"
          "'language-profanity': 'other'"
        "})>"
      "}"
    };

  gt_dbus_queue_set_server_func (fixture->queue, get_app_filter_server_cb,
                                 (gpointer) &get_app_filter_data);

  app_filter = epc_get_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                   fixture->valid_uid,
                                   FALSE, NULL, &local_error);

  g_assert_no_error (local_error);
  g_assert_nonnull (app_filter);

  /* Check the OARS filter properties. Each OARS value should have been parsed
   * correctly, except for the unknown `other` one. */
  g_assert_cmpuint (epc_app_filter_get_user_id (app_filter), ==, fixture->valid_uid);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "violence-bloodshed"), ==,
                   EPC_APP_FILTER_OARS_VALUE_NONE);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "violence-sexual"), ==,
                   EPC_APP_FILTER_OARS_VALUE_MILD);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "violence-fantasy"), ==,
                   EPC_APP_FILTER_OARS_VALUE_MODERATE);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "violence-realistic"), ==,
                   EPC_APP_FILTER_OARS_VALUE_INTENSE);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "language-profanity"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "unlisted-category"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);
}

/* Test that getting an #EpcAppFilter containing only an `AppFilter` property
 * from the mock D-Bus service works, and that the #EpcAppFilter methods use
 * appropriate defaults.
 *
 * The mock D-Bus replies are generated in get_app_filter_server_cb(). */
static void
test_app_filter_bus_get_defaults (BusFixture    *fixture,
                                  gconstpointer  test_data)
{
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  const GetAppFilterData get_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .properties = "{"
        "'AppFilter': <(false, @as [])>"
      "}"
    };
  g_autofree const gchar **oars_sections = NULL;

  gt_dbus_queue_set_server_func (fixture->queue, get_app_filter_server_cb,
                                 (gpointer) &get_app_filter_data);

  app_filter = epc_get_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                   fixture->valid_uid,
                                   FALSE, NULL, &local_error);

  g_assert_no_error (local_error);
  g_assert_nonnull (app_filter);

  /* Check the default values for the properties. */
  g_assert_cmpuint (epc_app_filter_get_user_id (app_filter), ==, fixture->valid_uid);
  oars_sections = epc_app_filter_get_oars_sections (app_filter);
  g_assert_cmpuint (g_strv_length ((gchar **) oars_sections), ==, 0);
  g_assert_cmpint (epc_app_filter_get_oars_value (app_filter, "violence-bloodshed"), ==,
                   EPC_APP_FILTER_OARS_VALUE_UNKNOWN);
  g_assert_true (epc_app_filter_is_user_installation_allowed (app_filter));
  g_assert_false (epc_app_filter_is_system_installation_allowed (app_filter));
}

/* Test that epc_get_app_filter() returns an appropriate error if the mock D-Bus
 * service reports that the given user cannot be found.
 *
 * The mock D-Bus replies are generated inline. */
static void
test_app_filter_bus_get_error_invalid_user (BusFixture    *fixture,
                                            gconstpointer  test_data)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDBusMethodInvocation) invocation = NULL;
  g_autofree gchar *error_message = NULL;
  g_autoptr(EpcAppFilter) app_filter = NULL;

  epc_get_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                            fixture->missing_uid,
                            FALSE, NULL, async_result_cb, &result);

  /* Handle the FindUserById() call and claim the user doesn’t exist. */
  gint64 user_id;
  invocation =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, fixture->missing_uid);

  error_message = g_strdup_printf ("Failed to look up user with uid %u.", fixture->missing_uid);
  g_dbus_method_invocation_return_dbus_error (invocation,
                                              "org.freedesktop.Accounts.Error.Failed",
                                              error_message);

  /* Get the get_app_filter() result. */
  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);
  app_filter = epc_get_app_filter_finish (result, &local_error);

  g_assert_error (local_error,
                  EPC_APP_FILTER_ERROR, EPC_APP_FILTER_ERROR_INVALID_USER);
  g_assert_null (app_filter);
}

/* Test that epc_get_app_filter() returns an appropriate error if the mock D-Bus
 * service reports that the properties of the given user can’t be accessed due
 * to permissions.
 *
 * The mock D-Bus replies are generated inline. */
static void
test_app_filter_bus_get_error_permission_denied (BusFixture    *fixture,
                                                 gconstpointer  test_data)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDBusMethodInvocation) invocation1 = NULL;
  g_autoptr(GDBusMethodInvocation) invocation2 = NULL;
  g_autofree gchar *object_path = NULL;
  g_autoptr(EpcAppFilter) app_filter = NULL;

  epc_get_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                            fixture->valid_uid,
                            FALSE, NULL, async_result_cb, &result);

  /* Handle the FindUserById() call. */
  gint64 user_id;
  invocation1 =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, fixture->valid_uid);

  object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%u", (uid_t) user_id);
  g_dbus_method_invocation_return_value (invocation1, g_variant_new ("(o)", object_path));

  /* Handle the Properties.GetAll() call and return a permission denied error. */
  const gchar *property_interface;
  invocation2 =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        object_path,
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll", "(&s)", &property_interface);
  g_assert_cmpstr (property_interface, ==, "com.endlessm.ParentalControls.AppFilter");

  g_dbus_method_invocation_return_dbus_error (invocation2,
                                              "org.freedesktop.Accounts.Error.PermissionDenied",
                                              "Not authorized");

  /* Get the get_app_filter() result. */
  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);
  app_filter = epc_get_app_filter_finish (result, &local_error);

  g_assert_error (local_error,
                  EPC_APP_FILTER_ERROR, EPC_APP_FILTER_ERROR_PERMISSION_DENIED);
  g_assert_null (app_filter);
}

/* Test that epc_get_app_filter() returns an appropriate error if the mock D-Bus
 * service replies with no app filter properties (implying that it hasn’t sent
 * the property values because of permissions).
 *
 * The mock D-Bus replies are generated inline. */
static void
test_app_filter_bus_get_error_permission_denied_missing (BusFixture    *fixture,
                                                         gconstpointer  test_data)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDBusMethodInvocation) invocation1 = NULL;
  g_autoptr(GDBusMethodInvocation) invocation2 = NULL;
  g_autofree gchar *object_path = NULL;
  g_autoptr(EpcAppFilter) app_filter = NULL;

  epc_get_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                            fixture->valid_uid,
                            FALSE, NULL, async_result_cb, &result);

  /* Handle the FindUserById() call. */
  gint64 user_id;
  invocation1 =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, fixture->valid_uid);

  object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%u", (uid_t) user_id);
  g_dbus_method_invocation_return_value (invocation1, g_variant_new ("(o)", object_path));

  /* Handle the Properties.GetAll() call and return an empty array due to not
   * having permission to access the properties. The code actually keys off the
   * presence of the AppFilter property, since that was the first one to be
   * added. */
  const gchar *property_interface;
  invocation2 =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        object_path,
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll", "(&s)", &property_interface);
  g_assert_cmpstr (property_interface, ==, "com.endlessm.ParentalControls.AppFilter");

  g_dbus_method_invocation_return_value (invocation2, g_variant_new ("(a{sv})", NULL));

  /* Get the get_app_filter() result. */
  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);
  app_filter = epc_get_app_filter_finish (result, &local_error);

  g_assert_error (local_error,
                  EPC_APP_FILTER_ERROR, EPC_APP_FILTER_ERROR_PERMISSION_DENIED);
  g_assert_null (app_filter);
}

/* Test that epc_get_app_filter() returns an error if the mock D-Bus service
 * reports an unrecognised error.
 *
 * The mock D-Bus replies are generated inline. */
static void
test_app_filter_bus_get_error_unknown (BusFixture    *fixture,
                                       gconstpointer  test_data)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDBusMethodInvocation) invocation = NULL;
  g_autoptr(EpcAppFilter) app_filter = NULL;

  epc_get_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                            fixture->valid_uid,
                            FALSE, NULL, async_result_cb, &result);

  /* Handle the FindUserById() call and return a bogus error. */
  gint64 user_id;
  invocation =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, fixture->valid_uid);

  g_dbus_method_invocation_return_dbus_error (invocation,
                                              "org.freedesktop.Accounts.Error.NewAndInterestingError",
                                              "This is a fake error message "
                                              "which libeos-parental-controls "
                                              "will never have seen before, "
                                              "but must still handle correctly");

  /* Get the get_app_filter() result. */
  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);
  app_filter = epc_get_app_filter_finish (result, &local_error);

  /* We don’t actually care what error is actually used here. */
  g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR);
  g_assert_null (app_filter);
}

/* Generic mock accountsservice implementation which handles properties being
 * set on a mock User object, and compares their values to the given
 * `expected_*` ones.
 *
 * If @error_index is non-negative, it gives the index of a Set() call to return
 * the given @dbus_error_name and @dbus_error_message from, rather than
 * accepting the property value from the caller. If @error_index is negative,
 * all Set() calls will be accepted. */
typedef struct
{
  uid_t expected_uid;

  /* All GVariants in text format: */
  const gchar *expected_app_filter_value;  /* (nullable) */
  const gchar *expected_oars_filter_value;  /* (nullable) */
  const gchar *expected_allow_user_installation_value;  /* (nullable) */
  const gchar *expected_allow_system_installation_value;  /* (nullable) */

  gint error_index;  /* -1 to return no error */
  const gchar *dbus_error_name;  /* NULL to return no error */
  const gchar *dbus_error_message;  /* NULL to return no error */
} SetAppFilterData;

static const gchar *
set_app_filter_data_get_expected_property_value (const SetAppFilterData *data,
                                                 const gchar            *property_name)
{
  if (g_str_equal (property_name, "AppFilter"))
    return data->expected_app_filter_value;
  else if (g_str_equal (property_name, "OarsFilter"))
    return data->expected_oars_filter_value;
  else if (g_str_equal (property_name, "AllowUserInstallation"))
    return data->expected_allow_user_installation_value;
  else if (g_str_equal (property_name, "AllowSystemInstallation"))
    return data->expected_allow_system_installation_value;
  else
    g_assert_not_reached ();
}

/* This is run in a worker thread. */
static void
set_app_filter_server_cb (GtDBusQueue *queue,
                          gpointer     user_data)
{
  const SetAppFilterData *data = user_data;
  g_autoptr(GDBusMethodInvocation) find_invocation = NULL;
  g_autofree gchar *object_path = NULL;

  g_assert ((data->error_index == -1) == (data->dbus_error_name == NULL));
  g_assert ((data->dbus_error_name == NULL) == (data->dbus_error_message == NULL));

  /* Handle the FindUserById() call. */
  gint64 user_id;
  find_invocation =
      gt_dbus_queue_assert_pop_message (queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, data->expected_uid);

  object_path = g_strdup_printf ("/org/freedesktop/Accounts/User%u", (uid_t) user_id);
  g_dbus_method_invocation_return_value (find_invocation, g_variant_new ("(o)", object_path));

  /* Handle the Properties.Set() calls. */
  const gchar *expected_properties[] =
    {
      "AppFilter",
      "OarsFilter",
      "AllowUserInstallation",
      "AllowSystemInstallation",
    };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (expected_properties); i++)
    {
      const gchar *property_interface;
      const gchar *property_name;
      g_autoptr(GVariant) property_value = NULL;
      g_autoptr(GDBusMethodInvocation) property_invocation = NULL;
      g_autoptr(GVariant) expected_property_value = NULL;

      property_invocation =
          gt_dbus_queue_assert_pop_message (queue,
                                            object_path,
                                            "org.freedesktop.DBus.Properties",
                                            "Set", "(&s&sv)", &property_interface,
                                            &property_name, &property_value);
      g_assert_cmpstr (property_interface, ==, "com.endlessm.ParentalControls.AppFilter");
      g_assert_cmpstr (property_name, ==, expected_properties[i]);

      if (data->error_index >= 0 && (gsize) data->error_index == i)
        {
          g_dbus_method_invocation_return_dbus_error (property_invocation,
                                                      data->dbus_error_name,
                                                      data->dbus_error_message);
          break;
        }
      else
        {
          expected_property_value = g_variant_new_parsed (set_app_filter_data_get_expected_property_value (data, property_name));
          assert_cmpvariant (property_value, expected_property_value);

          g_dbus_method_invocation_return_value (property_invocation, NULL);
        }
    }
}

/* Test that setting an #EpcAppFilter on the mock D-Bus service works. The
 * @test_data is a boolean value indicating whether to do the call
 * synchronously (%FALSE) or asynchronously (%TRUE).
 *
 * The mock D-Bus replies are generated in set_app_filter_server_cb(), which is
 * used for both synchronous and asynchronous calls. */
static void
test_app_filter_bus_set (BusFixture    *fixture,
                         gconstpointer  test_data)
{
  gboolean success;
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  gboolean test_async = GPOINTER_TO_UINT (test_data);
  const SetAppFilterData set_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .expected_app_filter_value = "(false, ['/usr/bin/false', '/usr/bin/banned', 'app/org.gnome.Nasty/x86_64/stable'])",
      .expected_oars_filter_value = "('oars-1.1', { 'violence-fantasy': 'intense' })",
      .expected_allow_user_installation_value = "true",
      .expected_allow_system_installation_value = "true",
      .error_index = -1,
    };

  /* Build an app filter. */
  epc_app_filter_builder_blacklist_path (&builder, "/usr/bin/false");
  epc_app_filter_builder_blacklist_path (&builder, "/usr/bin/banned");
  epc_app_filter_builder_blacklist_flatpak_ref (&builder, "app/org.gnome.Nasty/x86_64/stable");
  epc_app_filter_builder_set_oars_value (&builder, "violence-fantasy", EPC_APP_FILTER_OARS_VALUE_INTENSE);
  epc_app_filter_builder_set_allow_user_installation (&builder, TRUE);
  epc_app_filter_builder_set_allow_system_installation (&builder, TRUE);

  app_filter = epc_app_filter_builder_end (&builder);

  /* Set the mock service function and set the filter. */
  gt_dbus_queue_set_server_func (fixture->queue, set_app_filter_server_cb,
                                 (gpointer) &set_app_filter_data);

  if (test_async)
    {
      g_autoptr(GAsyncResult) result = NULL;

      epc_set_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                                fixture->valid_uid, app_filter,
                                FALSE, NULL, async_result_cb, &result);

      while (result == NULL)
        g_main_context_iteration (NULL, TRUE);
      success = epc_set_app_filter_finish (result, &local_error);
    }
  else
    {
      success = epc_set_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                    fixture->valid_uid, app_filter,
                                    FALSE, NULL, &local_error);
    }

  g_assert_no_error (local_error);
  g_assert_true (success);
}

/* Test that epc_set_app_filter() returns an appropriate error if the mock D-Bus
 * service reports that the given user cannot be found.
 *
 * The mock D-Bus replies are generated inline. */
static void
test_app_filter_bus_set_error_invalid_user (BusFixture    *fixture,
                                            gconstpointer  test_data)
{
  gboolean success;
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDBusMethodInvocation) invocation = NULL;
  g_autofree gchar *error_message = NULL;

  /* Use the default app filter. */
  app_filter = epc_app_filter_builder_end (&builder);

  epc_set_app_filter_async (gt_dbus_queue_get_client_connection (fixture->queue),
                            fixture->missing_uid, app_filter,
                            FALSE, NULL, async_result_cb, &result);

  /* Handle the FindUserById() call and claim the user doesn’t exist. */
  gint64 user_id;
  invocation =
      gt_dbus_queue_assert_pop_message (fixture->queue,
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById", "(x)", &user_id);
  g_assert_cmpint (user_id, ==, fixture->missing_uid);

  error_message = g_strdup_printf ("Failed to look up user with uid %u.", fixture->missing_uid);
  g_dbus_method_invocation_return_dbus_error (invocation,
                                              "org.freedesktop.Accounts.Error.Failed",
                                              error_message);

  /* Get the set_app_filter() result. */
  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);
  success = epc_set_app_filter_finish (result, &local_error);

  g_assert_error (local_error,
                  EPC_APP_FILTER_ERROR, EPC_APP_FILTER_ERROR_INVALID_USER);
  g_assert_false (success);
}

/* Test that epc_set_app_filter() returns an appropriate error if the mock D-Bus
 * service replies with a permission denied error when setting properties.
 *
 * The mock D-Bus replies are generated in set_app_filter_server_cb(). */
static void
test_app_filter_bus_set_error_permission_denied (BusFixture    *fixture,
                                                 gconstpointer  test_data)
{
  gboolean success;
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  const SetAppFilterData set_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .error_index = 0,
      .dbus_error_name = "org.freedesktop.Accounts.Error.PermissionDenied",
      .dbus_error_message = "Not authorized",
    };

  /* Use the default app filter. */
  app_filter = epc_app_filter_builder_end (&builder);

  gt_dbus_queue_set_server_func (fixture->queue, set_app_filter_server_cb,
                                 (gpointer) &set_app_filter_data);

  success = epc_set_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                fixture->valid_uid, app_filter,
                                FALSE, NULL, &local_error);

  g_assert_error (local_error,
                  EPC_APP_FILTER_ERROR, EPC_APP_FILTER_ERROR_PERMISSION_DENIED);
  g_assert_false (success);
}

/* Test that epc_set_app_filter() returns an error if the mock D-Bus service
 * reports an unrecognised error.
 *
 * The mock D-Bus replies are generated in set_app_filter_server_cb(). */
static void
test_app_filter_bus_set_error_unknown (BusFixture    *fixture,
                                       gconstpointer  test_data)
{
  gboolean success;
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  const SetAppFilterData set_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .error_index = 0,
      .dbus_error_name = "org.freedesktop.Accounts.Error.NewAndInterestingError",
      .dbus_error_message = "This is a fake error message which "
                            "libeos-parental-controls will never have seen "
                            "before, but must still handle correctly",
    };

  /* Use the default app filter. */
  app_filter = epc_app_filter_builder_end (&builder);

  gt_dbus_queue_set_server_func (fixture->queue, set_app_filter_server_cb,
                                 (gpointer) &set_app_filter_data);

  success = epc_set_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                fixture->valid_uid, app_filter,
                                FALSE, NULL, &local_error);

  g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR);
  g_assert_false (success);
}

/* Test that epc_set_app_filter() returns an error if the mock D-Bus service
 * reports an InvalidArgs error with a given one of its Set() calls.
 *
 * @test_data contains a property index encoded with GINT_TO_POINTER(),
 * indicating which Set() call to return the error on, since the calls are made
 * in series.
 *
 * The mock D-Bus replies are generated in set_app_filter_server_cb(). */
static void
test_app_filter_bus_set_error_invalid_property (BusFixture    *fixture,
                                                gconstpointer  test_data)
{
  gboolean success;
  g_auto(EpcAppFilterBuilder) builder = EPC_APP_FILTER_BUILDER_INIT ();
  g_autoptr(EpcAppFilter) app_filter = NULL;
  g_autoptr(GError) local_error = NULL;
  const SetAppFilterData set_app_filter_data =
    {
      .expected_uid = fixture->valid_uid,
      .expected_app_filter_value = "(false, @as [])",
      .expected_oars_filter_value = "('oars-1.1', @a{ss} [])",
      .expected_allow_user_installation_value = "true",
      .expected_allow_system_installation_value = "false",
      .error_index = GPOINTER_TO_INT (test_data),
      .dbus_error_name = "org.freedesktop.DBus.Error.InvalidArgs",
      .dbus_error_message = "Mumble mumble something wrong with the filter value",
    };

  /* Use the default app filter. */
  app_filter = epc_app_filter_builder_end (&builder);

  gt_dbus_queue_set_server_func (fixture->queue, set_app_filter_server_cb,
                                 (gpointer) &set_app_filter_data);

  success = epc_set_app_filter (gt_dbus_queue_get_client_connection (fixture->queue),
                                fixture->valid_uid, app_filter,
                                FALSE, NULL, &local_error);

  g_assert_error (local_error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_false (success);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/app-filter/error-quark", test_app_filter_error_quark);
  g_test_add_func ("/app-filter/types", test_app_filter_types);
  g_test_add_func ("/app-filter/refs", test_app_filter_refs);

  g_test_add ("/app-filter/builder/stack/non-empty", BuilderFixture, NULL,
              builder_set_up_stack, test_app_filter_builder_non_empty,
              builder_tear_down_stack);
  g_test_add ("/app-filter/builder/stack/empty", BuilderFixture, NULL,
              builder_set_up_stack, test_app_filter_builder_empty,
              builder_tear_down_stack);
  g_test_add ("/app-filter/builder/stack2/non-empty", BuilderFixture, NULL,
              builder_set_up_stack2, test_app_filter_builder_non_empty,
              builder_tear_down_stack2);
  g_test_add ("/app-filter/builder/stack2/empty", BuilderFixture, NULL,
              builder_set_up_stack2, test_app_filter_builder_empty,
              builder_tear_down_stack2);
  g_test_add ("/app-filter/builder/heap/non-empty", BuilderFixture, NULL,
              builder_set_up_heap, test_app_filter_builder_non_empty,
              builder_tear_down_heap);
  g_test_add ("/app-filter/builder/heap/empty", BuilderFixture, NULL,
              builder_set_up_heap, test_app_filter_builder_empty,
              builder_tear_down_heap);
  g_test_add_func ("/app-filter/builder/copy/empty",
                   test_app_filter_builder_copy_empty);
  g_test_add_func ("/app-filter/builder/copy/full",
                   test_app_filter_builder_copy_full);

  g_test_add_func ("/app-filter/appinfo", test_app_filter_appinfo);

  g_test_add ("/app-filter/bus/get/async", BusFixture, GUINT_TO_POINTER (TRUE),
              bus_set_up, test_app_filter_bus_get, bus_tear_down);
  g_test_add ("/app-filter/bus/get/sync", BusFixture, GUINT_TO_POINTER (FALSE),
              bus_set_up, test_app_filter_bus_get, bus_tear_down);
  g_test_add ("/app-filter/bus/get/whitelist", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_whitelist, bus_tear_down);
  g_test_add ("/app-filter/bus/get/all-oars-values", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_all_oars_values, bus_tear_down);
  g_test_add ("/app-filter/bus/get/defaults", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_defaults, bus_tear_down);

  g_test_add ("/app-filter/bus/get/error/invalid-user", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_error_invalid_user, bus_tear_down);
  g_test_add ("/app-filter/bus/get/error/permission-denied", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_error_permission_denied, bus_tear_down);
  g_test_add ("/app-filter/bus/get/error/permission-denied-missing", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_error_permission_denied_missing, bus_tear_down);
  g_test_add ("/app-filter/bus/get/error/unknown", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_get_error_unknown, bus_tear_down);

  g_test_add ("/app-filter/bus/set/async", BusFixture, GUINT_TO_POINTER (TRUE),
              bus_set_up, test_app_filter_bus_set, bus_tear_down);
  g_test_add ("/app-filter/bus/set/sync", BusFixture, GUINT_TO_POINTER (FALSE),
              bus_set_up, test_app_filter_bus_set, bus_tear_down);

  g_test_add ("/app-filter/bus/set/error/invalid-user", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_set_error_invalid_user, bus_tear_down);
  g_test_add ("/app-filter/bus/set/error/permission-denied", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_set_error_permission_denied, bus_tear_down);
  g_test_add ("/app-filter/bus/set/error/unknown", BusFixture, NULL,
              bus_set_up, test_app_filter_bus_set_error_unknown, bus_tear_down);
  g_test_add ("/app-filter/bus/set/error/invalid-property/app-filter",
              BusFixture, GINT_TO_POINTER (0), bus_set_up,
              test_app_filter_bus_set_error_invalid_property, bus_tear_down);
  g_test_add ("/app-filter/bus/set/error/invalid-property/oars-filter",
              BusFixture, GINT_TO_POINTER (1), bus_set_up,
              test_app_filter_bus_set_error_invalid_property, bus_tear_down);
  g_test_add ("/app-filter/bus/set/error/invalid-property/allow-user-installation",
              BusFixture, GINT_TO_POINTER (2), bus_set_up,
              test_app_filter_bus_set_error_invalid_property, bus_tear_down);
  g_test_add ("/app-filter/bus/set/error/invalid-property/allow-system-installation",
              BusFixture, GINT_TO_POINTER (3), bus_set_up,
              test_app_filter_bus_set_error_invalid_property, bus_tear_down);

  return g_test_run ();
}
