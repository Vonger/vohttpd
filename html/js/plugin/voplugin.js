function voplugin_refresh() {
    plugin_list = vohttpd_call("plugin_list");
    $("#voplugin-table").empty();
    for(id = 0; id < plugin_list.plugins.length; id++) {
        plugin = plugin_list.plugins[id].name;
        note = plugin_list.plugins[id].note;
        name = plugin.substring(0, plugin.lastIndexOf("."));

        html = "<tr><td><div><b>" + plugin + "</b><p class=\"help-block\">" + note + "</p><div></td><td>";
        html += "<button class=\"btn btn-default btn-xs\" id=\"voplugin-unload-" + name + "\" name=\"" + plugin + "\"><span class=\"glyphicon glyphicon-ban-circle\"></span></button>";
        html += "<button class=\"btn btn-default btn-xs\" id=\"voplugin-remove-" + name + "\" name=\"" + plugin + "\"><span class=\"glyphicon glyphicon-remove\"></span></button>";
        html += "<button class=\"btn btn-default btn-xs\" id=\"voplugin-detail-" + name + "\" name=\"" + plugin + "\"><span class=\"glyphicon glyphicon-th-list\"></span></button>";
        html += "</td></tr>"
        $("#voplugin-table").append(html);

        $("#voplugin-unload-" + name).bind("click", function() {
            var plugin = $(this).attr("name");
            var name = plugin.substring(0, plugin.lastIndexOf("."));
            var param = "id=\"voplugin-unload-button-" + name + "\" parameter=\"" + plugin + "\"";
            var id = "voplugin-box-unload-" + name;
            vohttpd_message("Notify", "Are you sure you want to unload " + plugin + "?", id, "Unload", param);
            $("#voplugin-unload-button-" + name).bind("click", function() {
                var plugin = $(this).attr("parameter");
                var result = vohttpd_call("plugin_unload", plugin);
                vohttpd_message("Status", result.status);
                voplugin_refresh();
            });
        });

        $("#voplugin-remove-" + name).bind("click", function() {
            var plugin = $(this).attr("name");
            var name = plugin.substring(0, plugin.lastIndexOf("."));
            var param = "id=\"voplugin-remove-button-" + name + "\" parameter=\"" + plugin + "\"";
            var id = "voplugin-box-remove-" + name;
            vohttpd_message("Notify", "Are you sure you want to remove " + plugin + "?", id, "Remove", param);
            $("#voplugin-remove-button-" + name).bind("click", function() {
                var plugin = $(this).attr("parameter");
                var result = vohttpd_call("plugin_uninstall", plugin);
                vohttpd_message("Status", result.status);
                voplugin_refresh();
            });
        });

        $("#voplugin-detail-" + name).bind("click", function() {
            plugin = $(this).attr("name");
            name = plugin.substring(0, plugin.lastIndexOf("."));

            $("#voplugin-detail-dialog-" + name).remove();
            interface_list = vohttpd_call("plugin_list_interface", $(this).attr("name"));
            if(interface_list.status !== "success")
                vohttpd_message("<div style=\"color:#F00\">ERROR</div>", interface_list.status);

            html = "<div class=\"modal fade\" id=\"voplugin-detail-dialog-" + name + "\" tabindex=\"-1\" role=\"dialog\">";
            html += "<div class=\"modal-dialog\"><div class=\"modal-content\"><div class=\"modal-header\">";
            html += "<button type=\"button\" class=\"close\" data-dismiss=\"modal\"><span aria-hidden=\"true\">&times;</span><span class=\"sr-only\">Close</span></button>";
            html += "<h4 class=\"modal-title\" id=\"voplugin-modal-title\">" + plugin + "</h4></div>";
            html += "<table class=\"table modal-body\"><tbody id=\"voplugin-detail-table\">";
            for(id = 0; id < interface_list.interfaces.length; id++) {
                status = interface_list.interfaces[id].status === "loaded" ? "success" : "danger";
                html += "<tr>";
                html += "<td><h5>" + interface_list.interfaces[id].name + "</h5></td>";
                html += "<td><p class=\"help-block\">" + interface_list.interfaces[id].note + "</p></td>";
                html += "<td><span class=\"label label-" + status + "\">" + interface_list.interfaces[id].status + "</span></td>";
                html += "</tr>";
            }
            html += "<tr><td></td><td></td><td><button type=\"button\" class=\"btn btn-default\" data-dismiss=\"modal\">Close</button></td></tr>";
            html += "</tbody></table></div></div></div>";
            $(html).modal("show");
        });
    }
}

function voplugin_main() {
    var id_body = "vohttpd-panel-body-voplugin";
    var id = "vohttpd-panel-voplugin", html;
    vohttpd_create_panel("Plugin Control Panel", id, id_body);

    html = "<h4>Installed</h4>";
    html += "<div><table class=\"table\" style=\"margin-bottom:0px\"><tbody id=\"voplugin-table\"></tbody></table></div>";
    $("#" + id_body).append(html);

    html = "<h4>Install</h4>";
    html += "<form id=\"voplugin-install-form\" action=\"cgi-bin/plugin_install\" method=\"post\" enctype =\"multipart/form-data\">"
    html += "<input id=\"voplugin-file-input\" type=\"file\" name=\"filename\" style=\"display:none\">";
    html += "<div class=\"input-group\"><input id=\"voplugin-file-input-alter\" readonly=\"readonly\" type=\"text\" class=\"form-control\">";
    html += "<span class=\"input-group-btn\"><button class=\"btn btn-default\" type=\"button\" onclick=\"$('input[id=voplugin-file-input]').click();\">Choose</button>";
    html += "<button class=\"btn btn-primary\" type=\"submit\">Upload & Install</button></span></div></form>";
    $("#" + id_body).append(html);

    $("#voplugin-file-input").change(function() {
        $("#voplugin-file-input-alter").val($(this).val());
    });

    $("#voplugin-install-form").on("submit", function(event) {
        event.preventDefault();

        var files = $("#voplugin-file-input").prop("files");
        var name = $("#voplugin-file-input").prop("name");
        var form_data = new FormData();
        form_data.append(name, files[0]);

        var raw = $.ajax({
            url:$(this).attr("action"), type:$(this).attr("method"),
            beforeSend:function(jq, opt) { opt.data = form_data; },
            async:false,
        });
        vohttpd_message("Notify", $.parseJSON(raw.responseText).status);
        voplugin_refresh();
    });

    voplugin_refresh();
}

voplugin_main();
